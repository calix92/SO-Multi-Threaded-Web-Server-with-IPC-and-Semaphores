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

// Lógica de PRODUTOR: Coloca na fila circular
void enqueue_connection(shared_data_t* data, semaphores_t* sems, int client_fd) {
    // 1. Esperar por espaço livre (sem_wait(empty))
    sem_wait(sems->empty_slots);
    
    // 2. Bloquear acesso à fila (Mutex)
    sem_wait(sems->queue_mutex);
    
    // 3. Inserir socket
    data->queue.sockets[data->queue.rear] = client_fd;
    data->queue.rear = (data->queue.rear + 1) % MAX_QUEUE_SIZE;
    data->queue.count++;
    
    // 4. Libertar acesso e sinalizar novo item
    sem_post(sems->queue_mutex);
    sem_post(sems->filled_slots);
}

void master_run(server_config_t *config) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Master [PID %d]: A iniciar na porta %d com %d workers...\n", getpid(), config->port, config->num_workers);

    // IPC Setup
    shared_data_t* shm = create_shared_memory();
    if (!shm) { perror("Master: Falha SHM"); exit(1); }

    semaphores_t sems;
    if (init_semaphores(&sems, config->max_queue_size) != 0) {
        perror("Master: Falha Semáforos");
        destroy_shared_memory(shm);
        exit(1);
    }

    int server_socket = create_server_socket(config->port);
    if (server_socket < 0) {
        perror("Master: Falha Socket");
        destroy_semaphores(&sems);
        destroy_shared_memory(shm);
        exit(1);
    }

    // Fork Workers
    pid_t pids[config->num_workers];
    for (int i = 0; i < config->num_workers; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            close(server_socket); // Filho não precisa do listener
            worker_main(i);       // Chama o worker (Consumer)
            exit(0);
        }
    }

    printf("Master: Workers iniciados. A aceitar conexões...\n");

    // Loop Principal (PRODUTOR)
    while (keep_running) {
        // Monitorização simples: a cada 30 timeouts (aprox 30s) mostra stats
        // Mas a prioridade é o accept. Usamos um contador simples aqui ou alarm.
        // Para simplificar e garantir performance no accept, mostramos stats apenas ao sair ou com sinal.
        // Se quiseres timer real, precisas de sigaction com SIGALRM.
        
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (errno == EINTR) break; // Parar no Ctrl+C
            perror("Master: Erro no accept");
            continue;
        }

        enqueue_connection(shm, &sems, client_fd);
    }

    // Cleanup
    printf("\nMaster: A encerrar...\n");
    display_stats(shm, &sems); // Mostra estatísticas finais

    for (int i = 0; i < config->num_workers; i++) {
        if (pids[i] > 0) kill(pids[i], SIGTERM);
    }
    for (int i = 0; i < config->num_workers; i++) wait(NULL);

    close(server_socket);
    destroy_semaphores(&sems);
    destroy_shared_memory(shm);
    printf("Master: Limpeza concluída.\n");
}