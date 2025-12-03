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

// Lógica de CONSUMIDOR: Retira da fila circular
int dequeue_connection(shared_data_t* data, semaphores_t* sems) {
    // 1. Esperar item disponível (sem_wait(filled))
    if (sem_wait(sems->filled_slots) != 0) return -1;
    if (!worker_running) return -1;

    // 2. Bloquear acesso à fila
    sem_wait(sems->queue_mutex);
    
    // 3. Retirar socket
    int client_fd = data->queue.sockets[data->queue.front];
    data->queue.front = (data->queue.front + 1) % MAX_QUEUE_SIZE;
    data->queue.count--;
    
    // 4. Libertar acesso e sinalizar espaço livre
    sem_post(sems->queue_mutex);
    sem_post(sems->empty_slots);
    
    return client_fd;
}

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
    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);

    printf("Worker %d: Pronto (Consumer Mode)!\n", worker_id);

    while (worker_running) {
        // Vai buscar trabalho à SHM (bloqueia aqui se vazio)
        int client_fd = dequeue_connection(shm, &sems);
        
        if (client_fd >= 0) {
            thread_pool_dispatch(pool, client_fd);
        }
    }

    destroy_thread_pool(pool);
    cache_destroy(cache);
    // Workers não destroem SHM/Semáforos
    exit(0);
}