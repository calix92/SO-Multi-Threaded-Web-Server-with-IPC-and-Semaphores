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

volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    worker_running = 0;
}

void worker_main(int worker_id, int server_socket) {
    signal(SIGINT, worker_signal_handler);
    signal(SIGTERM, worker_signal_handler);

    printf("Worker %d pronto para aceitar conexões.\n", worker_id);

    // Ligar à SHM (necessário para estatísticas mais tarde)
    shared_data_t* shm = create_shared_memory();
    semaphores_t sems;
    init_semaphores(&sems, 100);

    // Criar Thread Pool (10 threads hardcoded ou via config)
    thread_pool_t* pool = create_thread_pool(10);
    if (!pool) {
        perror("Worker: Erro ao criar pool");
        exit(1);
    }

    while (worker_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // Bloqueio no accept protegido por mutex para evitar "Thundering Herd"
        // (Vários processos a acordar para 1 conexão)
        sem_wait(sems.queue_mutex);
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        sem_post(sems.queue_mutex);

        if (client_fd < 0) {
            // Se foi interrompido pelo shutdown, saímos do loop
            if (worker_running) perror("Worker: Erro no accept");
            continue;
        }

        // Passa o cliente para uma thread tratar
        thread_pool_dispatch(pool, client_fd);
    }

    printf("Worker %d a encerrar...\n", worker_id);
    destroy_thread_pool(pool);
    exit(0);
}