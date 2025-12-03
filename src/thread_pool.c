// src/thread_pool.c
#include "thread_pool.h"
#include "http.h"
#include "cache.h"      // <--- Necessário para a cache
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

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
    return "text/plain";
}

// Alterado para receber 'pool' (para aceder à cache)
void handle_client(thread_pool_t* pool, int client_fd) {
    // Forçar logs imediatos
    setbuf(stdout, NULL);
    
    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        http_request_t req;
        
        if (parse_http_request(buffer, &req) == 0) {
            
            // 1. Determinar o caminho do ficheiro
            char file_path[512];
            if (strcmp(req.path, "/") == 0) {
                sprintf(file_path, "www/index.html");
            } else {
                sprintf(file_path, "www%s", req.path);
            }

            printf("[Thread] Pedido: %s -> %s\n", req.path, file_path);

            // 2. TENTAR LER DA CACHE PRIMEIRO
            // Nota: pool->cache pode ser NULL se não tiver sido inicializado no worker
            cache_entry_t* cached_entry = NULL;
            if (pool->cache) {
                cached_entry = cache_get(pool->cache, file_path);
            }

            if (cached_entry) {
                // === CACHE HIT ===
                printf("[Thread] CACHE HIT: Servindo %s da memória.\n", file_path);
                send_http_response(client_fd, 200, "OK", get_mime_type(file_path), cached_entry->data, cached_entry->size);
            } 
            else {
                // === CACHE MISS (Ler do Disco) ===
                FILE* file = fopen(file_path, "rb");
                if (file) {
                    fseek(file, 0, SEEK_END);
                    long fsize = ftell(file);
                    fseek(file, 0, SEEK_SET);

                    char* body = malloc(fsize);
                    if (body) {
                        size_t read_bytes = fread(body, 1, fsize, file);
                        
                        // Enviar resposta ao cliente
                        send_http_response(client_fd, 200, "OK", get_mime_type(file_path), body, read_bytes);
                        
                        // GUARDAR NA CACHE (se houver cache e ficheiro < 1MB)
                        if (pool->cache && read_bytes < 1024 * 1024) {
                            cache_put(pool->cache, file_path, body, read_bytes);
                            printf("[Thread] CACHE MISS: %s guardado na cache.\n", file_path);
                        }
                        
                        free(body);
                    }
                    fclose(file);
                } else {
                    // 404 Not Found
                    const char* msg = "404 Not Found";
                    send_http_response(client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
                }
            }

        } else {
            send_http_response(client_fd, 400, "Bad Request", "text/html", NULL, 0);
        }
    }
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
            // Passamos 'pool' para a handle_client poder usar a cache
            handle_client(pool, task->client_fd);
            free(task);
        }
    }
    return NULL;
}

void thread_pool_dispatch(thread_pool_t* pool, int client_fd) {
    task_t* new_task = malloc(sizeof(task_t));
    if (!new_task) return;
    
    new_task->client_fd = client_fd;
    new_task->next = NULL;

    pthread_mutex_lock(&pool->mutex);
    
    if (pool->tail) {
        pool->tail->next = new_task;
    } else {
        pool->head = new_task;
    }
    pool->tail = new_task;

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

// Atualizado para aceitar ponteiro da cache (definido no .h que alteraste antes)
thread_pool_t* create_thread_pool(int num_threads, cache_t* cache) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    pool->num_threads = num_threads;
    pool->head = NULL;
    pool->tail = NULL;
    pool->shutdown = 0;
    pool->cache = cache; // Guardar referência da cache

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    }

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