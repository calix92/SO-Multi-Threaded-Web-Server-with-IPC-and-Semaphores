// src/master.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>
#include "master.h"
#include "config.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "worker.h"

volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 128) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Esta função deixa de ser usada nesta estratégia, mas pode ficar aqui
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
    // 1. Configurar sinais
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Master: A iniciar servidor na porta %d com %d workers...\n", 
           config->port, config->num_workers);

    // 2. Criar Memória Partilhada
    shared_data_t* shm = create_shared_memory();
    if (!shm) {
        perror("Master: Erro ao criar memoria partilhada");
        exit(EXIT_FAILURE);
    }

    // 3. Inicializar Semáforos
    semaphores_t sems;
    if (init_semaphores(&sems, config->max_queue_size) != 0) {
        perror("Master: Erro ao iniciar semaforos");
        destroy_shared_memory(shm);
        exit(EXIT_FAILURE);
    }

    // 4. Criar Socket do Servidor
    int server_socket = create_server_socket(config->port);
    if (server_socket < 0) {
        perror("Master: Erro ao criar socket");
        destroy_semaphores(&sems);
        destroy_shared_memory(shm);
        exit(EXIT_FAILURE);
    }

    // 5. Fork Workers
    pid_t pids[config->num_workers];
    
    for (int i = 0; i < config->num_workers; i++) {
        pids[i] = fork();
        
        if (pids[i] < 0) {
            perror("Master: Erro no fork");
            continue;
        }
        
        if (pids[i] == 0) {
            // === PROCESSO FILHO (WORKER) ===
            // Passamos o server_socket para o filho!
            worker_main(i, server_socket); 
            exit(0);
        }
    }

    printf("Master: Todos os workers iniciados. A aguardar sinais (Master inativo no IO)...\n");

    // 6. Loop Principal do Master (Apenas espera para morrer)
    while (keep_running) {
        sleep(1);
    }

    // 7. Cleanup
    printf("\nMaster: A encerrar servidor...\n");
    
    for (int i = 0; i < config->num_workers; i++) {
        if (pids[i] > 0) kill(pids[i], SIGTERM);
    }
    
    for (int i = 0; i < config->num_workers; i++) {
        wait(NULL);
    }

    close(server_socket);
    destroy_semaphores(&sems);
    destroy_shared_memory(shm);
    printf("Master: Limpeza concluída.\n");
}