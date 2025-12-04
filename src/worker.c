// src/worker.c
#define _POSIX_C_SOURCE 200809L // IMPORTANTE PARA O ERRO DO SIGACTION
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
#include <sys/poll.h>
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

    printf("Worker %d [PID %d]: Pronto (Serialized Accept Mode)!\n", worker_id, getpid());

    shared_data_t* shm = create_shared_memory();
    if (!shm) exit(1);

    semaphores_t sems;
    if (init_semaphores(&sems, 0) < 0) exit(1);

    cache_t* cache = cache_init(10);
    if (!cache) exit(1);

    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);
    if (!pool) exit(1);

    while (worker_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 1. Tentar ganhar o direito de fazer accept (EXCLUSÃO MÚTUA)
        // Se formos interrompidos por sinal (CTRL+C), saímos
        if (sem_wait(sems.queue_mutex) != 0) {
            if (errno == EINTR) break; 
            continue;
        }

        // 2. Verificar se há alguém à porta (Poll com timeout curto)
        // Timeout de 500ms para não prender o mutex para sempre se ninguém conectar
        struct pollfd fds[1];
        fds[0].fd = server_socket;
        fds[0].events = POLLIN;

        int ret = poll(fds, 1, 500); 

        if (ret > 0) {
            // Há cliente! Aceitar
            int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
            
            // Libertar o mutex IMEDIATAMENTE para outro worker poder tentar
            sem_post(sems.queue_mutex);

            if (client_fd >= 0) {
                // Processar (o descritor é válido neste processo!)
                thread_pool_dispatch(pool, client_fd);
            } else {
                perror("Erro no accept");
            }
        } else {
            // Timeout ou Erro: Libertar o mutex e dar oportunidade a outros
            sem_post(sems.queue_mutex);
            // Pequena pausa (opcional) para evitar busy-loop intenso se estiver vazio
            // usleep(1000); 
        }
    }

    destroy_thread_pool(pool);
    cache_destroy(cache);
    // Não fechar server_socket aqui se quiseres evitar warnings de double free no OS, mas é boa prática
    // close(server_socket); 
    exit(0);
}