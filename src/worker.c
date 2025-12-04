// src/worker.c
#define _POSIX_C_SOURCE 200809L
#include "worker.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    worker_running = 0;
}

void worker_main(int worker_id, int server_socket) {
    setbuf(stdout, NULL);
    
    struct sigaction sa;
    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Worker %d [PID %d]: Pronto (Serialized Accept)!\n", worker_id, getpid());

    shared_data_t* shm = create_shared_memory();
    if (!shm) exit(1);

    semaphores_t sems;
    // Abrir semáforos existentes
    if (init_semaphores(&sems, 0) < 0) exit(1);

    cache_t* cache = cache_init(10);
    if (!cache) exit(1);

    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);
    if (!pool) exit(1);

    while (worker_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 1. Proteção: Apenas um worker deve tentar accept de cada vez
        if (sem_wait(sems.queue_mutex) != 0) {
            if (errno == EINTR) break;
            continue;
        }

        // 2. Accept
        // Usamos accept normal. Como temos o mutex, estamos seguros.
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        // 3. Libertar o mutex IMEDIATAMENTE após ter a conexão
        sem_post(sems.queue_mutex);

        if (client_fd >= 0) {
            // Sucesso! Passar para as threads processarem
            thread_pool_dispatch(pool, client_fd);
        } else {
            if (errno != EINTR) {
                perror("Worker Accept Error");
            }
        }
    }

    destroy_thread_pool(pool);
    cache_destroy(cache);
    exit(0);
}