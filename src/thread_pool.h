// src/thread_pool.h
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "cache.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "config.h"

// Estrutura para fila interna
typedef struct task {
    int client_fd;
    struct task* next;
} task_t;

typedef struct {
    pthread_t* threads;
    int num_threads;
    
    // Fila de tarefas
    task_t* head;
    task_t* tail;
    
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;

    cache_t* cache;

    server_config_t* config;
    
    // Permite acesso à SHM e aos Semáforos
    shared_data_t* shm; 
    semaphores_t* sems;
} thread_pool_t;

// Assinatura da função de criação (inclui os novos ponteiros IPC)
thread_pool_t* create_thread_pool(int num_threads, cache_t* cache, shared_data_t* shm, semaphores_t* sems, server_config_t* config);

void destroy_thread_pool(thread_pool_t* pool);
void thread_pool_dispatch(thread_pool_t* pool, int client_fd);

#endif