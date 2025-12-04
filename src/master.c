// src/master.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>
#include <time.h>
#include "master.h"
#include "config.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "worker.h"
#include "stats.h"
#include <errno.h>

volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt falhou");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind falhou");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 128) < 0) {
        perror("Listen falhou");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void master_run(server_config_t *config) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Master [PID %d]: A iniciar na porta %d...\n", getpid(), config->port);

    // 1. Limpeza Preventiva (Importante!)
    shm_unlink("/webserver_shm"); 
    sem_unlink("/ws_empty"); sem_unlink("/ws_filled");
    sem_unlink("/ws_queue_mutex"); sem_unlink("/ws_stats_mutex"); sem_unlink("/ws_log_mutex");

    // 2. IPC Setup
    shared_data_t* shm = create_shared_memory();
    if (!shm) { perror("Master: Falha SHM"); exit(1); }
    // INICIALIZAR MEMÓRIA A ZEROS
    memset(shm, 0, sizeof(shared_data_t)); 
    shm->stats.start_time = time(NULL);

    semaphores_t sems;
    if (init_semaphores(&sems, config->max_queue_size) != 0) {
        perror("Master: Falha Semáforos");
        exit(1);
    }

    // 3. Socket
    int server_socket = create_server_socket(config->port);
    if (server_socket < 0) exit(1);

    // 4. Fork Workers
    pid_t pids[config->num_workers];
    for (int i = 0; i < config->num_workers; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            worker_main(i, server_socket); // Worker herda o listening socket
            exit(0);
        }
    }

    printf("Master: Workers iniciados. Servidor Online.\n");

    // 5. Loop de Monitorização e Estatísticas
    // O Master está livre para fazer gestão, pois os Workers tratam das conexões.
    int countdown = 0;
    
    while (keep_running) {
        sleep(1); // Espera 1 segundo
        
        countdown++;
        // Verifica se passou o tempo definido (30s) para mostrar stats
        if (countdown >= config->timeout_seconds) {
            display_stats(shm, &sems); // <--- ESTA É A FUNÇÃO QUE FALTAVA
            countdown = 0;
        }
    }

    // Cleanup
    printf("\nMaster: A encerrar...\n");
    for (int i = 0; i < config->num_workers; i++) {
        if (pids[i] > 0) kill(pids[i], SIGTERM);
    }
    for (int i = 0; i < config->num_workers; i++) wait(NULL);

    close(server_socket);
    destroy_semaphores(&sems);
    destroy_shared_memory(shm);
    printf("Master: Limpeza concluída.\n");
}