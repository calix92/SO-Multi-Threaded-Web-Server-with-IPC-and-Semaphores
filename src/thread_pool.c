// src/thread_pool.c - CÓDIGO COMPLETO
#include "thread_pool.h"
#include "http.h"
#include "cache.h"
#include "stats.h"          // Incluir para update_stats
#include "logger.h"         // Incluir para log_request
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

// Função auxiliar para determinar o MIME type (Já estava funcional)
const char* get_mime_type(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "application/javascript";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0) return "image/jpeg";
    if (strcmp(dot, ".gif") == 0) return "image/gif";
    return "text/plain";
}

// NOVO: Função auxiliar para servir páginas de erro em HTML
void send_error_page_file(int fd, int status, const char* status_msg, const char* file_path, 
                          shared_data_t* shm, semaphores_t* sems, const char* req_path) {
    
    FILE* file = fopen(file_path, "rb");
    size_t bytes_sent = 0;
    
    if (!file) {
        // Fallback: Se a página de erro não existir, envia texto simples (500 Internal Error)
        const char* fallback_body = (status == 404) ? "404 Not Found" : "500 Internal Error";
        bytes_sent = strlen(fallback_body);
        send_http_response(fd, status, status_msg, "text/plain", fallback_body, bytes_sent);
        // Não faz log se falhou a abrir o ficheiro de fallback
        return; 
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* body = malloc(fsize);
    if (body) {
        size_t read_bytes = fread(body, 1, fsize, file);
        bytes_sent = read_bytes;
        
        // Envia a página de erro em HTML
        send_http_response(fd, status, status_msg, "text/html", body, read_bytes);
        
        free(body);
    }
    fclose(file);

    // ATUALIZA LOGGING E ESTATÍSTICAS após erro
    // Não conseguimos o IP do cliente aqui, vamos usar um placeholder
    log_request(sems->log_mutex, "127.0.0.1", "GET", req_path, status, bytes_sent);
    update_stats(shm, sems, status, bytes_sent);
}


// Função principal de tratamento de pedidos
void handle_client(thread_pool_t* pool, int client_fd) {
    setbuf(stdout, NULL);
    
    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    shared_data_t* shm = pool->shm;
    semaphores_t* sems = pool->sems;
    
    int status = 500; // Assume 500 Internal Error por defeito
    size_t bytes_sent = 0;
    char req_path[512] = ""; // Para o log
    
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    http_request_t req;
    
    if (parse_http_request(buffer, &req) != 0) {
        // 400 Bad Request
        status = 400;
        send_http_response(client_fd, status, "Bad Request", "text/html", NULL, 0);
        // Não faz log de pedidos mal formados se não tiver path
    } else {
        strcpy(req_path, req.path);
        
        // 1. Determinar o caminho do ficheiro
        char file_path[512];
        if (strcmp(req.path, "/") == 0) {
            sprintf(file_path, "www/index.html");
        } else {
            sprintf(file_path, "www%s", req.path);
        }

        // 2. TENTAR LER DA CACHE PRIMEIRO
        cache_entry_t* cached_entry = NULL;
        if (pool->cache) {
            cached_entry = cache_get(pool->cache, file_path);
        }

        if (cached_entry) {
            // CACHE HIT
            printf("[Thread] CACHE HIT: %s\n", file_path);
            bytes_sent = cached_entry->size;
            status = 200;
            
            // Só envia body se NÃO for HEAD
            if (strcmp(req.method, "HEAD") == 0) {
                send_http_response(client_fd, 200, "OK", get_mime_type(file_path), NULL, bytes_sent);
            } else {
                send_http_response(client_fd, 200, "OK", get_mime_type(file_path), cached_entry->data, bytes_sent);
            }
        } 
        else {
            // CACHE MISS
            FILE* file = fopen(file_path, "rb");
            if (file) {
                fseek(file, 0, SEEK_END);
                long fsize = ftell(file);
                fseek(file, 0, SEEK_SET);

                // Se for HEAD, não precisamos de ler o ficheiro todo para a memória
                if (strcmp(req.method, "HEAD") == 0) {
                    send_http_response(client_fd, 200, "OK", get_mime_type(file_path), NULL, fsize);
                    bytes_sent = 0; // No body transferred, but content-length sent
                    // Nota: para stats, talvez queiras contar o fsize ou 0. Normalmente conta-se bytes transferidos.
                } else {
                    char* body = malloc(fsize);
                    if (body) {
                        size_t read_bytes = fread(body, 1, fsize, file);
                        bytes_sent = read_bytes;
                        status = 200;
                        
                        send_http_response(client_fd, 200, "OK", get_mime_type(file_path), body, bytes_sent);
                        
                        // Cache logic...
                        if (pool->cache && read_bytes < 1024 * 1024) {
                            cache_put(pool->cache, file_path, body, read_bytes);
                        }
                        free(body);
                    }
                }
                fclose(file);
            } else {
                // 404 logic...
                send_error_page_file(client_fd, 404, "Not Found", "www/errors/404.html", shm, sems, req_path);
                status = 404;
            }
        }
    }
    
    // ATUALIZA LOGGING E ESTATÍSTICAS (apenas se for 200 ou 404/500, e se tiver path)
    if (status >= 200 && status < 600 && req_path[0] != '\0') {
        // Nota: O log_request e update_stats devem ser chamados só uma vez, 
        // mas a chamada para 404/500 já faz update, então só fazemos aqui para 200.
        if (status == 200) {
            log_request(sems->log_mutex, "127.0.0.1", req.method, req_path, status, bytes_sent);
            update_stats(shm, sems, status, bytes_sent);
        }
    }

    close(client_fd);
}

// ... Resto da thread_pool.c ...
// worker_thread (já chama handle_client(pool, task->client_fd) - OK)
// thread_pool_dispatch (OK)

// A função create_thread_pool deve ser atualizada para receber os ponteiros IPC
thread_pool_t* create_thread_pool(int num_threads, cache_t* cache, shared_data_t* shm, semaphores_t* sems) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;

    // ... alocação de threads ...

    pool->cache = cache;
    pool->shm = shm;      // <--- TEM DE TER ISTO
    pool->sems = sems;    // <--- TEM DE TER ISTO

    // ... resto da inicialização ...
    return pool;
}
void destroy_thread_pool(thread_pool_t* pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool);
}