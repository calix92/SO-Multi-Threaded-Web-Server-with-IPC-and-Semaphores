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
#include <string.h>
#include <stdatomic.h>

static atomic_int worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    atomic_store(&worker_running, 0);
}

void worker_main(int worker_id, int server_socket) {
    setbuf(stdout, NULL);
    
    // Configurar gestão de sinais
    struct sigaction sa;
    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Worker %d [PID %d]: Pronto!\n", worker_id, getpid());

    // Ligar à memória partilhada e semáforos já criados pelo Master
    shared_data_t* shm = create_shared_memory();
    semaphores_t sems;
    if (init_semaphores(&sems, 0) < 0) exit(1);

    // Inicializar Cache e Thread Pool (Aqui vivem os bónus!)
    cache_t* cache = cache_init(10); // 10MB cache
    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);

    // Loop Principal: Worker aceita conexões
    while (atomic_load(&worker_running)) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 1. Bloquear acesso ao accept (Exclusão Mútua entre processos)
        // Isto evita "Thundering Herd" e garante estabilidade
        if (sem_wait(sems.queue_mutex) != 0) {
            if (errno == EINTR) break; 
            continue;
        }

        // 2. Aceitar a conexão
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        // 3. Libertar IMEDIATAMENTE o mutex para outro worker poder aceitar
        sem_post(sems.queue_mutex);

        // 4. Processar
        if (client_fd >= 0) {
            // Enviar para as threads (Onde está o Keep-Alive e Dashboard)
            thread_pool_dispatch(pool, client_fd);
        } else {
            if (errno == EINTR) break;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Worker Accept Error");
            }
        }
    }

    // Limpeza
    destroy_thread_pool(pool);
    cache_destroy(cache);
    exit(0);
}