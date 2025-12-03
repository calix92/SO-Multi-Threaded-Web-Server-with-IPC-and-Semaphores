// src/worker.c - CÓDIGO COMPLETO

// src/worker.c
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
#include "cache.h"

volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    worker_running = 0;
}

void worker_main(int worker_id, int server_socket) {
    // DESLIGAR BUFFER DO PRINTF (Logs aparecem logo!)
    setbuf(stdout, NULL);

    signal(SIGINT, worker_signal_handler);
    signal(SIGTERM, worker_signal_handler);

    printf("Worker %d [PID %d] iniciado. A ligar recursos...\n", worker_id, getpid());

    shared_data_t* shm = create_shared_memory();
    if (!shm) {
        perror("Worker: Falha na SHM");
        exit(1);
    }

    semaphores_t sems;
    if (init_semaphores(&sems, 100) < 0) {
        perror("Worker: Falha nos semáforos");
        destroy_shared_memory(shm);
        exit(1);
    }

    cache_t* cache = cache_init(10);
    if (!cache) {
        perror("Worker: Falha ao criar cache");
        destroy_shared_memory(shm);
        exit(1);
    }

    // ALTERAÇÃO CRÍTICA: Passar shm e sems para o create_thread_pool
    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);
    if (!pool) {
        perror("Worker: Falha na pool");
        cache_destroy(cache);
        destroy_shared_memory(shm);
        exit(1);
    }

    printf("Worker %d pronto! A entrar no loop de aceitação.\n", worker_id);

    while (worker_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        sem_wait(sems.queue_mutex);
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        sem_post(sems.queue_mutex);

        if (client_fd < 0) {
            if (worker_running) perror("Worker: Erro no accept");
            continue;
        }

        printf("Worker %d: Conexão recebida! FD=%d. A despachar para a thread...\n", worker_id, client_fd);
        thread_pool_dispatch(pool, client_fd);
    }

    printf("Worker %d a encerrar...\n", worker_id);
    destroy_thread_pool(pool);
    cache_destroy(cache);
    exit(0);
}