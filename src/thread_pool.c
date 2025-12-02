// src/thread_pool.c
#include "thread_pool.h"
#include "http.h"
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
    return "text/plain";
}

void handle_client(int client_fd) {
    // Forçar logs imediatos
    setbuf(stdout, NULL);
    
    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        http_request_t req;
        
        if (parse_http_request(buffer, &req) == 0) {
            printf("[Thread] Pedido: %s\n", req.path);
            
            char file_path[512];
            if (strcmp(req.path, "/") == 0) sprintf(file_path, "www/index.html");
            else sprintf(file_path, "www%s", req.path);

            FILE* file = fopen(file_path, "rb");
            if (file) {
                fseek(file, 0, SEEK_END);
                long fsize = ftell(file);
                fseek(file, 0, SEEK_SET);

                char* body = malloc(fsize);
                if (body) {
                    size_t read_bytes = fread(body, 1, fsize, file);
                    send_http_response(client_fd, 200, "OK", get_mime_type(file_path), body, read_bytes);
                    free(body);
                }
                fclose(file);
            } else {
                // 404 Simples
                const char* msg = "404 Not Found";
                send_http_response(client_fd, 404, "Not Found", "text/plain", msg, strlen(msg));
            }
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
            if (pool->head == NULL) pool->tail = NULL;
        }
        pthread_mutex_unlock(&pool->mutex);

        if (task) {
            handle_client(task->client_fd);
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
    if (pool->tail) pool->tail->next = new_task;
    else pool->head = new_task;
    pool->tail = new_task;
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

thread_pool_t* create_thread_pool(int num_threads) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    pool->num_threads = num_threads;
    pool->head = NULL;
    pool->tail = NULL;
    pool->shutdown = 0;
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    for (int i = 0; i < num_threads; i++) 
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    return pool;
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