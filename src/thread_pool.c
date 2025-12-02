// src/thread_pool.c
#include "thread_pool.h"
#include "http.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>      // FIX: strlen
#include <sys/socket.h>  // FIX: recv

void handle_client(int client_fd) {
    char buffer[4096];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        http_request_t req;
        if (parse_http_request(buffer, &req) == 0) {
            // Resposta temporária de sucesso
            const char* body = "<html><body><h1>Funciona! Ola da Thread.</h1></body></html>";
            send_http_response(client_fd, 200, "OK", "text/html", body, strlen(body));
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
    
    if (pool->tail) {
        pool->tail->next = new_task;
    } else {
        pool->head = new_task;
    }
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