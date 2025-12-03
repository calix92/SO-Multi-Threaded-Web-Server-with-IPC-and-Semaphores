// src/master.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include "master.h"
#include "config.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "worker.h"
#include "stats.h"

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

// PRODUTOR: Põe na memória partilhada
void enqueue_connection(shared_data_t* data, semaphores_t* sems, int client_fd) {
    sem_wait(sems->empty_slots);
    sem_wait(sems->queue_mutex);
    
    data->queue.sockets[data->queue.rear] = client_fd;
    data->queue.rear = (data->queue.rear + 1) % MAX_QUEUE_SIZE;
    data->queue.count++;
    
    sem_post(sems->queue_mutex);
    sem_post(sems->filled_slots);
}

void master_run(server_config_t *config) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Master [PID %d]: A iniciar na porta %d...\n", getpid(), config->port);

    // 1. IPC
    shared_data_t* shm = create_shared_memory();
    if (!shm) { perror("Master: Falha SHM"); exit(1); }

    semaphores_t sems;
    if (init_semaphores(&sems, config->max_queue_size) != 0) {
        perror("Master: Falha Semáforos");
        destroy_shared_memory(shm);
        exit(1);
    }

    // 2. Socket
    int server_socket = create_server_socket(config->port);
    if (server_socket < 0) {
        perror("Master: Falha Socket");
        destroy_semaphores(&sems);
        destroy_shared_memory(shm);
        exit(1);
    }

    // 3. Fork Workers
    pid_t pids[config->num_workers];
    for (int i = 0; i < config->num_workers; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            close(server_socket); // Filho não precisa do socket de escuta
            worker_main(i);       // CORRIGIDO: Só passa o ID
            exit(0);
        }
    }

    printf("Master: Workers iniciados. A aceitar conexões...\n");

    // 4. Loop Principal (PRODUTOR)
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        // Master faz o accept
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) break;
            perror("Master: Erro no accept");
            continue;
        }

        // Põe na fila para os workers
        enqueue_connection(shm, &sems, client_fd);
    }

    // Cleanup
    printf("\nMaster: A encerrar...\n");
    display_stats(shm, &sems);

    for (int i = 0; i < config->num_workers; i++) {
        if (pids[i] > 0) kill(pids[i], SIGTERM);
    }
    for (int i = 0; i < config->num_workers; i++) wait(NULL);

    close(server_socket);
    destroy_semaphores(&sems);
    destroy_shared_memory(shm);
    printf("Master: Limpeza concluída.\n");
}