#include "worker.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h> // Necessário para sockaddr_in
#include "cache.h"
#include <sys/poll.h>

volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    worker_running = 0;
}

void worker_main(int worker_id, int server_socket) {
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

    thread_pool_t* pool = create_thread_pool(10, cache, shm, &sems);
    if (!pool) exit(1);

printf("Worker %d: Pronto (Accept Mode)!\n", worker_id);

    while (worker_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 1. Bloquear acesso ao socket (Exclusão mútua entre workers)
        sem_wait(sems.queue_mutex);

        // 2. VERIFICAÇÃO COM POLL (TIMEOUT)
        // Criamos uma estrutura para monitorizar o server_socket
        struct pollfd fds[1];
        fds[0].fd = server_socket;
        fds[0].events = POLLIN; // Queremos saber se há dados para ler (conexão a entrar)

        // Espera até 1000ms (1 segundo). Retorna > 0 se houver conexão, 0 se timeout.
        int ret = poll(fds, 1, 1000);

        if (ret > 0) {
            // Há uma conexão à espera! Podemos fazer accept sem bloquear "para sempre"
            int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
            
            // Libertar acesso para outros workers IMEDIATAMENTE após o accept
            sem_post(sems.queue_mutex);

            if (client_fd >= 0) {
                // Processar o pedido
                thread_pool_dispatch(pool, client_fd);
            } else {
                perror("Erro no accept");
            }
        } else {
            // Timeout (passou 1 seg e ninguém se conectou) OU Erro no poll
            // Libertar o semáforo para que outros workers possam tentar (ou para não bloquear o shutdown)
            sem_post(sems.queue_mutex);
            
            // O código agora volta ao início do while. 
            // Se worker_running for 0 (porque fizeste Ctrl+C), o ciclo termina aqui!
        }
    }

    destroy_thread_pool(pool);
    cache_destroy(cache);
    close(server_socket); // Fecha a cópia local do socket
    exit(0);
}