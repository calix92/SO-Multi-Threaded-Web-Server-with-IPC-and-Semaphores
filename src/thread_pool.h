#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

// Estrutura para uma tarefa na fila interna
typedef struct task {
    int client_fd;
    struct task* next;
} task_t;

typedef struct {
    pthread_t* threads;
    int num_threads;
    
    // Fila interna de tarefas
    task_t* head;
    task_t* tail;
    
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;
} thread_pool_t;

thread_pool_t* create_thread_pool(int num_threads);
void destroy_thread_pool(thread_pool_t* pool);

// Nova função para o Worker passar trabalho às Threads
void thread_pool_dispatch(thread_pool_t* pool, int client_fd);

#endif