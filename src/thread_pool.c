// src/thread_pool.c
#include "thread_pool.h"
#include "http.h"
#include "cache.h"
#include "stats.h"
#include "logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>

#define KEEPALIVE_TIMEOUT 5 // segundos

const char* get_mime_type(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "application/javascript";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0) return "image/jpeg";
    if (strcmp(dot, ".gif") == 0) return "image/gif";
    if (strcmp(dot, ".pdf") == 0) return "application/pdf";
    return "text/plain";
}

// Helper atualizado com o parâmetro keep_alive=0 (erro fecha conexão por segurança)
void send_error_page_file(int fd, int status, const char* status_msg, const char* file_path, 
                          shared_data_t* shm, semaphores_t* sems, const char* req_path) {
    (void)shm; (void)sems; (void)req_path; // unused warning fix
    
    FILE* file = fopen(file_path, "rb");
    size_t bytes_sent = 0;
    
    if (!file) {
        const char* fallback_body = (status == 404) ? "404 Not Found" : "500 Internal Error";
        bytes_sent = strlen(fallback_body);
        // Erros forçam fecho de conexão (keep_alive = 0)
        send_http_response(fd, status, status_msg, "text/plain", fallback_body, bytes_sent, 0);
        return; 
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* body = malloc(fsize);
    if (body) {
        size_t read_bytes = fread(body, 1, fsize, file);
        send_http_response(fd, status, status_msg, "text/html", body, read_bytes, 0);
        free(body);
    }
    fclose(file);
}

void handle_client(thread_pool_t* pool, int client_fd) {
    setbuf(stdout, NULL);
    
    shared_data_t* shm = pool->shm;
    semaphores_t* sems = pool->sems;

    // 1. Configurar Timeout no Socket (Keep-Alive)
    struct timeval tv;
    tv.tv_sec = KEEPALIVE_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Incrementar Active Connections (uma vez por cliente)
    sem_wait(sems->stats_mutex);
    shm->stats.active_connections++;
    sem_post(sems->stats_mutex);

// Loop para processar múltiplos pedidos na mesma conexão
    while (1) {
        char buffer[8192];
        
        struct timeval start, end;
        gettimeofday(&start, NULL);

        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) break; // Cliente fechou ou timeout
        
        buffer[bytes_read] = '\0';
        
        // --- CORREÇÃO CRÍTICA 1: LIMPAR A MEMÓRIA ---
        http_request_t req;
        memset(&req, 0, sizeof(req)); 
        // --------------------------------------------

        int status = 500;
        size_t bytes_sent = 0;
        char req_path[512] = "";
        int is_cache_hit = 0;

        if (parse_http_request(buffer, &req) != 0) {
            send_http_response(client_fd, 400, "Bad Request", "text/html", NULL, 0, 0);
            break; // Sai do loop imediatamente
        }
        
        // --- CORREÇÃO CRÍTICA 2: KEEP-ALIVE INTELIGENTE ---
        // Assume FECHAR por defeito (para o 'ab' não bloquear)
        int keep_alive = 0; 
        
        // Só mantém aberto se for explicitamente HTTP/1.1
        if (strcasecmp(req.version, "HTTP/1.1") == 0) {
            keep_alive = 1;
        }
        
        // Se o cliente pediu para fechar, respeitamos sempre
        if (req.connection_close) {
            keep_alive = 0;
        }
        // --------------------------------------------------

        strcpy(req_path, req.path);
        // ... (resto do código continua igual: /stats, ficheiros, etc.)

        if (parse_http_request(buffer, &req) != 0) {
            status = 400;
            send_http_response(client_fd, 400, "Bad Request", "text/html", NULL, 0, 0);
            keep_alive = 0; // Forçar saída
        } else {
            strcpy(req_path, req.path);

            // --- DASHBOARD ---
            if (strcmp(req.path, "/stats") == 0) {
                sem_wait(sems->stats_mutex);
                time_t now = time(NULL);
                long uptime = now - shm->stats.start_time;
                double avg_time = (shm->stats.total_requests > 0) ? 
                    (double)shm->stats.total_response_time_ms / shm->stats.total_requests : 0;

                char body[8192];
                int body_len = snprintf(body, sizeof(body),
                    "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='3'><title>Stats</title>"
                    "<style>body{font-family:sans-serif;padding:20px;background:#f4f4f9} .card{background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}</style>"
                    "</head><body><div class='card'><h1>Server Dashboard</h1>"
                    "<p>Uptime: <b>%lds</b> | Active Conn: <b>%d</b></p>"
                    "<p>Total Req: <b>%ld</b> | Avg Time: <b>%.2fms</b></p>"
                    "<p>Bytes: <b>%ld</b> | Hits: <b>%ld</b></p>"
                    "<p>200: %ld | 404: %ld | 500: %ld</p></div></body></html>",
                    uptime, shm->stats.active_connections, shm->stats.total_requests, avg_time,
                    shm->stats.bytes_transferred, shm->stats.cache_hits,
                    shm->stats.status_200, shm->stats.status_404, shm->stats.status_500
                );
                sem_post(sems->stats_mutex);
                
                send_http_response(client_fd, 200, "OK", "text/html", body, body_len, 1);
                status = 200; bytes_sent = body_len;
            }
            // --- SERVIR FICHEIRO ---
            else {
                char file_path[1024];
                // --- LÓGICA VIRTUAL HOSTS ---
                const char* base_root = pool->config->document_root; // Root padrão

                // Procurar se o host corresponde a algum VHost configurado
                for (int i = 0; i < pool->config->vhost_count; i++) {
                    if (strcmp(req.host, pool->config->vhosts[i].hostname) == 0) {
                        base_root = pool->config->vhosts[i].root;
                        break;
                    }
                }

                if (strcmp(req.path, "/") == 0) 
                    snprintf(file_path, sizeof(file_path), "%s/index.html", base_root);
                else 
                    snprintf(file_path, sizeof(file_path), "%s%s", base_root, req.path);
                // ---------------------------

                // Cache
                size_t c_size = 0;
                void* c_data = pool->cache ? cache_get(pool->cache, file_path, &c_size) : NULL;

                if (c_data) {
                    is_cache_hit = 1; bytes_sent = c_size; status = 200;
                    send_http_response(client_fd, 200, "OK", get_mime_type(file_path), 
                                     (strcmp(req.method, "HEAD")==0 ? NULL : c_data), bytes_sent, 1);
                    free(c_data);
                } else {
                    FILE* f = fopen(file_path, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    
                    // Lógica de Range
                    if (req.range_start != -1 && req.range_start < fsize) {
                        // É um pedido parcial!
                        long start = req.range_start;
                        long end = (req.range_end == -1 || req.range_end >= fsize) ? fsize - 1 : req.range_end;
                        size_t chunk_size = end - start + 1;

                        char* b = malloc(chunk_size);
                        if (b) {
                            fseek(f, start, SEEK_SET); // Saltar para o início pedido
                            fread(b, 1, chunk_size, f);
                            
                            // Enviar 206 Partial Content
                            send_http_partial_response(client_fd, get_mime_type(file_path), b, chunk_size, start, end, fsize, 1);
                            free(b);
                        }
                        bytes_sent = chunk_size; status = 206;
                    } 
                    else {
                        // Pedido Normal (200 OK) - Código antigo
                        fseek(f, 0, SEEK_SET);
                        if (strcmp(req.method, "HEAD") == 0) {
                            send_http_response(client_fd, 200, "OK", get_mime_type(file_path), NULL, fsize, 1);
                        } else {
                            char* b = malloc(fsize);
                            if (b) {
                                fread(b, 1, fsize, f);
                                send_http_response(client_fd, 200, "OK", get_mime_type(file_path), b, fsize, 1);
                                // (Opcional) Guardar em cache aqui
                                if (pool->cache && fsize < 1048576) cache_put(pool->cache, file_path, b, fsize);
                                free(b);
                            }
                        }
                        bytes_sent = fsize; status = 200;
                    }
                    fclose(f);
                    } else {
                        status = (errno == EACCES) ? 403 : 404;
                        send_error_page_file(client_fd, status, (status==403?"Forbidden":"Not Found"), 
                                           (status==403?"www/errors/403.html":"www/errors/404.html"), 
                                           shm, sems, req_path);
                        keep_alive = 0; // Erros fecham conexão
                    }
                }
            }
        }

        // Stats Update
        gettimeofday(&end, NULL);
        long dur = ((end.tv_sec - start.tv_sec)*1000000 + end.tv_usec - start.tv_usec) / 1000;
        if (req_path[0]) {
            log_request(sems->log_mutex, "127.0.0.1", req.method, req_path, status, bytes_sent);
            update_stats(shm, sems, status, bytes_sent, dur, is_cache_hit);
        }

        if (!keep_alive) break;
    }

    // Decrementar Active Connections ao sair
    sem_wait(sems->stats_mutex);
    shm->stats.active_connections--;
    sem_post(sems->stats_mutex);

    close(client_fd);
}

void* worker_thread(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        if (pool->shutdown && pool->head == NULL) {
            pthread_mutex_unlock(&pool->mutex); break;
        }
        task_t* task = pool->head;
        if (task) {
            pool->head = task->next;
            if (pool->head == NULL) pool->tail = NULL;
        }
        pthread_mutex_unlock(&pool->mutex);
        if (task) {
            handle_client(pool, task->client_fd);
            free(task);
        }
    }
    return NULL;
}

thread_pool_t* create_thread_pool(int num_threads, cache_t* cache, shared_data_t* shm, semaphores_t* sems, server_config_t* config) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    pool->config = config;
    pool->num_threads = num_threads;
    pool->head = NULL; pool->tail = NULL;
    pool->shutdown = 0; pool->cache = cache; pool->shm = shm; pool->sems = sems;
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    for (int i = 0; i < num_threads; i++) 
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    return pool;
}

void thread_pool_dispatch(thread_pool_t* pool, int client_fd) {
    task_t* task = malloc(sizeof(task_t));
    if (!task) { close(client_fd); return; }
    task->client_fd = client_fd; task->next = NULL;
    pthread_mutex_lock(&pool->mutex);
    if (pool->tail) pool->tail->next = task; else pool->head = task;
    pool->tail = task;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

void destroy_thread_pool(thread_pool_t* pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
    for (int i = 0; i < pool->num_threads; i++) pthread_join(pool->threads[i], NULL);
    free(pool->threads);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool);
}