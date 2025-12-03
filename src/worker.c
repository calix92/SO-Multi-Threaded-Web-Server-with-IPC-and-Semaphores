// src/worker.c
#include "worker.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "cache.h"

volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    worker_running = 0;
}

// CONSUMIDOR: Retira da fila
int dequeue_connection(shared_data_t* data, semaphores_t* sems) {
    // 1. Espera haver itens
    if (sem_wait(sems->filled_slots) != 0) return -1;
    if (!worker_running) return -1;

    // 2. Bloqueia acesso
    sem_wait(sems->queue_mutex);
    
    // 3. Retira item
    int client_fd = data->queue.sockets[data->queue.front];
    data->queue.front = (data->queue.front + 1) % MAX_QUEUE_SIZE;
    data->queue.count--;
    
    // 4. Liberta acesso
    sem_post(sems->queue_mutex);
    sem_post(sems->empty_slots);
    
    return client_fd;
}

// CORRIGIDO: Recebe apenas o worker_id
void worker_main(int worker_id) {
    setbuf(stdout, NULL);
    signal(SIGINT, worker_signal_handler);
    signal(SIGTERM, worker_signal_handler);

    printf("Worker %d [PID %d]: A iniciar...\n", worker_id, getpid());

    shared_data_t* shm = create_shared_memory();
    if (!shm) exit(1);

    semaphores_t sems;
    if (init_semaphores(&sems, 0) < 0) exit(1);

    cache_t* cache = cache_init(10);
    if (!cache) exit(1);

    // Cria a pool passando os ponteiros IPC
    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);
    if (!pool) exit(1);

    printf("Worker %d: Pronto (Consumer Mode)!\n", worker_id);

    while (worker_running) {
        // Vai buscar à memória partilhada em vez de fazer accept()
        int client_fd = dequeue_connection(shm, &sems);
        
        if (client_fd >= 0) {
            thread_pool_dispatch(pool, client_fd);
        }
    }

    destroy_thread_pool(pool);
    cache_destroy(cache);
    // Nota: Workers não destroem a SHM
    exit(0);
}