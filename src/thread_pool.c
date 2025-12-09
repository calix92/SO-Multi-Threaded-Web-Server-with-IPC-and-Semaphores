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
#include <time.h> // Necessário para time()

// Função auxiliar para determinar o MIME type
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

// Função auxiliar para servir páginas de erro em HTML
void send_error_page_file(int fd, int status, const char* status_msg, const char* file_path, 
                          shared_data_t* shm, semaphores_t* sems, const char* req_path) {
    
    FILE* file = fopen(file_path, "rb");
    size_t bytes_sent = 0;
    
    if (!file) {
        const char* fallback_body = (status == 404) ? "404 Not Found" : "500 Internal Error";
        bytes_sent = strlen(fallback_body);
        send_http_response(fd, status, status_msg, "text/plain", fallback_body, bytes_sent);
        return; 
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* body = malloc(fsize);
    if (body) {
        size_t read_bytes = fread(body, 1, fsize, file);
        bytes_sent = read_bytes;
        
        send_http_response(fd, status, status_msg, "text/html", body, read_bytes);
        
        free(body);
    }
    fclose(file);
}

void handle_client(thread_pool_t* pool, int client_fd) {
    setbuf(stdout, NULL);
    
    // --- 1. INICIAR CRONÓMETRO E CONEXÃO ATIVA ---
    struct timeval start, end;
    gettimeofday(&start, NULL);

    shared_data_t* shm = pool->shm;
    semaphores_t* sems = pool->sems;

    sem_wait(sems->stats_mutex);
    shm->stats.active_connections++;
    sem_post(sems->stats_mutex);
    // ---------------------------------------------

    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    int status = 500;
    size_t bytes_sent = 0;
    char req_path[512] = "";
    int is_cache_hit = 0; // Variável para controlar o hit
    
    if (bytes_read <= 0) {
        // Se falhar a leitura, temos de decrementar a conexão antes de sair
        sem_wait(sems->stats_mutex);
        shm->stats.active_connections--;
        sem_post(sems->stats_mutex);
        
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    http_request_t req;
    
    if (parse_http_request(buffer, &req) != 0) {
        status = 400;
        send_http_response(client_fd, status, "Bad Request", "text/html", NULL, 0);
    } else {
        
        // ============================================================
        //  DASHBOARD REAL-TIME (/stats) - FEATURE EXTRA (+5 pontos)
        // ============================================================
        if (strcmp(req.path, "/stats") == 0) {
            sem_wait(sems->stats_mutex); // Leitura segura dos dados partilhados

            time_t now = time(NULL);
            long uptime = now - shm->stats.start_time;

            double avg_time = 0;
            if (shm->stats.total_requests > 0)
                avg_time = (double)shm->stats.total_response_time_ms / shm->stats.total_requests;

            // Construir HTML da dashboard
            char body[8192]; 
            int body_len = snprintf(body, sizeof(body),
                "<!DOCTYPE html>"
                "<html lang='en'><head><meta charset='UTF-8'><title>Server Dashboard</title>"
                "<style>"
                "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: #f0f2f5; color: #333; }"
                ".container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }"
                "h1 { border-bottom: 2px solid #3498db; padding-bottom: 10px; color: #2c3e50; }"
                ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 20px; margin-top: 20px; }"
                ".card { background: #f8f9fa; padding: 15px; border-radius: 8px; text-align: center; border: 1px solid #e9ecef; }"
                ".value { font-size: 24px; font-weight: bold; color: #3498db; display: block; margin-bottom: 5px; }"
                ".label { font-size: 14px; color: #6c757d; text-transform: uppercase; letter-spacing: 1px; }"
                ".status-list { list-style: none; padding: 0; margin-top: 20px; }"
                ".status-list li { padding: 10px; border-bottom: 1px solid #eee; display: flex; justify-content: space-between; }"
                ".footer { margin-top: 30px; text-align: center; font-size: 12px; color: #aaa; }"
                "</style>"
                "<meta http-equiv='refresh' content='3'>" // Auto-refresh a cada 3s
                "</head><body>"
                "<div class='container'>"
                "<h1>Server Real-Time Dashboard</h1>"
                "<div class='grid'>"
                "  <div class='card'><span class='value'>%lds</span><span class='label'>Uptime</span></div>"
                "  <div class='card'><span class='value'>%d</span><span class='label'>Active Conn</span></div>"
                "  <div class='card'><span class='value'>%ld</span><span class='label'>Total Req</span></div>"
                "  <div class='card'><span class='value'>%.2f ms</span><span class='label'>Avg Latency</span></div>"
                "  <div class='card'><span class='value'>%ld</span><span class='label'>Total Bytes</span></div>"
                "  <div class='card'><span class='value'>%ld</span><span class='label'>Cache Hits</span></div>"
                "</div>"
                "<h3>HTTP Status Breakdown</h3>"
                "<ul class='status-list'>"
                "<li><span>200 OK</span> <strong>%ld</strong></li>"
                "<li><span>403 Forbidden</span> <strong>%ld</strong></li>"
                "<li><span>404 Not Found</span> <strong>%ld</strong></li>"
                "<li><span>500 Internal Error</span> <strong>%ld</strong></li>"
                "</ul>"
                "<div class='footer'>ConcurrentHTTP Server v1.0 | Updated: Automatic (3s)</div>"
                "</div></body></html>",
                uptime,
                shm->stats.active_connections,
                shm->stats.total_requests,
                avg_time,
                shm->stats.bytes_transferred,
                shm->stats.cache_hits,
                shm->stats.status_200,
                shm->stats.status_403,
                shm->stats.status_404,
                shm->stats.status_500
            );

            sem_post(sems->stats_mutex); // Libertar mutex

            send_http_response(client_fd, 200, "OK", "text/html", body, body_len);
            
            // Logar o pedido das estatísticas
            log_request(sems->log_mutex, "127.0.0.1", req.method, "/stats", 200, body_len);
            
            // Decrementar conexão ativa antes de sair
            sem_wait(sems->stats_mutex);
            shm->stats.active_connections--;
            sem_post(sems->stats_mutex);

            close(client_fd);
            return; // Sair para não tentar ler ficheiros do disco
        }
        // ============================================================

        strcpy(req_path, req.path);
        
        // Construção segura do caminho do ficheiro
        char file_path[1024]; 
    
        if (strcmp(req.path, "/") == 0) {
            snprintf(file_path, sizeof(file_path), "www/index.html");
        } else {
            snprintf(file_path, sizeof(file_path), "www%s", req.path);
        }

        // ---------------------------------------------------------
        // 1. Verificar Cache
        // ---------------------------------------------------------
        size_t cache_size = 0;
        void* cache_data = NULL; // Ponteiro para a CÓPIA dos dados

        if (pool->cache) {
            // cache_get agora devolve uma cópia segura e preenche o cache_size
            cache_data = cache_get(pool->cache, file_path, &cache_size);
        }

        if (cache_data) {
            // CACHE HIT
            printf("[Thread] CACHE HIT: %s\n", file_path);
            is_cache_hit = 1; 
            bytes_sent = cache_size;
            status = 200;
            
            if (strcmp(req.method, "HEAD") == 0) {
                send_http_response(client_fd, 200, "OK", get_mime_type(file_path), NULL, bytes_sent);
            } else {
                // Envia a cópia dos dados que recebemos
                send_http_response(client_fd, 200, "OK", get_mime_type(file_path), cache_data, bytes_sent);
            }
            
            free(cache_data); // IMPORTANTE: Libertar a cópia aqui para não haver leaks!
        } 
        else {
            // CACHE MISS - Tentar ler do disco
            FILE* file = fopen(file_path, "rb");
            
            if (file) {
                // Ficheiro existe e abriu com sucesso
                fseek(file, 0, SEEK_END);
                long fsize = ftell(file);
                fseek(file, 0, SEEK_SET);

                if (strcmp(req.method, "HEAD") == 0) {
                    send_http_response(client_fd, 200, "OK", get_mime_type(file_path), NULL, fsize);
                    bytes_sent = 0;
                } else {
                    char* body = malloc(fsize);
                    if (body) {
                        size_t read_bytes = fread(body, 1, fsize, file);
                        bytes_sent = read_bytes;
                        status = 200;
                        
                        send_http_response(client_fd, 200, "OK", get_mime_type(file_path), body, bytes_sent);
                        
                        // Guardar na cache se for pequeno
                        if (pool->cache && read_bytes < 1024 * 1024) {
                            cache_put(pool->cache, file_path, body, read_bytes);
                        }
                        free(body);
                    }
                }
                fclose(file);
            } else {
                // Ficheiro não abriu - Verificar errno para distinguir 403 de 404
                if (errno == EACCES) {
                    // Permissão negada -> 403 Forbidden
                    send_error_page_file(client_fd, 403, "Forbidden", "www/errors/403.html", shm, sems, req_path);
                    status = 403;
                } else {
                    // Não encontrado (ou outro erro) -> 404 Not Found
                    send_error_page_file(client_fd, 404, "Not Found", "www/errors/404.html", shm, sems, req_path);
                    status = 404;
                }
            }
        }
    }
    
    // --- 2. PARAR CRONÓMETRO E ATUALIZAR ESTATÍSTICAS ---
    gettimeofday(&end, NULL);
    long seconds = (end.tv_sec - start.tv_sec);
    long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);
    long duration_ms = micros / 1000;

    // Decrementar Active Connections
    sem_wait(sems->stats_mutex);
    shm->stats.active_connections--;
    sem_post(sems->stats_mutex);

    // Registar tudo (incluindo tempo e cache hit)
    if (req_path[0] != '\0') {
        log_request(sems->log_mutex, "127.0.0.1", req.method, req_path, status, bytes_sent);
        update_stats(shm, sems, status, bytes_sent, duration_ms, is_cache_hit);
    }
    // ----------------------------------------------------

    close(client_fd);
}

// Worker thread function
void* worker_thread(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        
        while (pool->head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        
        if (pool->shutdown && pool->head == NULL) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        task_t* task = pool->head;
        if (task) {
            pool->head = task->next;
            if (pool->head == NULL) {
                pool->tail = NULL;
            }
        }
        
        pthread_mutex_unlock(&pool->mutex);
        
        if (task) {
            handle_client(pool, task->client_fd);
            free(task);
        }
    }
    
    return NULL;
}

// Create thread pool
thread_pool_t* create_thread_pool(int num_threads, cache_t* cache, shared_data_t* shm, semaphores_t* sems) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->head = NULL;
    pool->tail = NULL;
    pool->shutdown = 0;
    pool->cache = cache;
    pool->shm = shm;
    pool->sems = sems;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->cond);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->cond);
            free(pool->threads);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

// Dispatch task to thread pool
void thread_pool_dispatch(thread_pool_t* pool, int client_fd) {
    task_t* task = malloc(sizeof(task_t));
    if (!task) {
        close(client_fd);
        return;
    }

    task->client_fd = client_fd;
    task->next = NULL;

    pthread_mutex_lock(&pool->mutex);

    if (pool->tail) {
        pool->tail->next = task;
    } else {
        pool->head = task;
    }
    pool->tail = task;

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

// Destroy thread pool
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