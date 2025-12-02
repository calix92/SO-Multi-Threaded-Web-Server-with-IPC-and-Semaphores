#include "worker.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "thread_pool.h"
#include "config.h"      // Para ter acesso às configs se necessário
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>

// Variável global para o signal handler do worker apanhar
volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    (void)signum;
    worker_running = 0;
}

void worker_main(int worker_id) {
    // 1. Tratamento de Sinais
    signal(SIGINT, worker_signal_handler);
    signal(SIGTERM, worker_signal_handler);

    printf("Worker %d iniciado (PID: %d)\n", worker_id, getpid());

    // 2. Ligar à Memória Partilhada (Já existente)
    // Nota: Como foi fork(), tecnicamente já temos acesso, mas 
    // é boa prática re-abrir ou garantir que temos os ponteiros certos.
    // Vamos assumir que criamos novos ponteiros para garantir limpeza.
    
    shared_data_t* shm = create_shared_memory(); // A função lida com o shm_open existente
    if (!shm) {
        perror("Worker: Erro ao ligar SHM");
        exit(1);
    }

    // 3. Ligar aos Semáforos
    semaphores_t sems;
    // O tamanho da queue aqui não é critico porque os semáforos já existem (O_CREAT sem O_EXCL)
    if (init_semaphores(&sems, 100) < 0) { 
        perror("Worker: Erro ao ligar Semáforos");
        exit(1);
    }

    // 4. Criar Thread Pool
    // O número de threads devia vir da config, vamos assumir 10 por default ou ler de config
    // Para simplificar, vou pôr 10 hardcoded ou tens de passar config para aqui.
    int threads_per_worker = 10; 
    thread_pool_t* pool = create_thread_pool(threads_per_worker);
    if (!pool) {
        perror("Worker: Erro ao criar pool");
        exit(1);
    }

    // 5. Loop de Processamento (Consumidor)
    while (worker_running) {
        // A. Esperar por trabalho (IPC)
        // Se sem_wait for interrompido por sinal, verificamos worker_running
        if (sem_wait(sems.filled_slots) < 0) {
            if (!worker_running) break;
            continue;
        }

        // B. Entrar na região crítica da fila
        if (sem_wait(sems.queue_mutex) < 0) {
             if (!worker_running) break;
             continue;
        }

        // C. Retirar socket da fila circular
        int client_fd = shm->queue.sockets[shm->queue.front];
        shm->queue.front = (shm->queue.front + 1) % MAX_QUEUE_SIZE;
        shm->queue.count--;

        // D. Sair da região crítica
        sem_post(sems.queue_mutex);
        sem_post(sems.empty_slots); // Avisar Produtor que há espaço

        // E. Enviar para a Thread Pool (Fila interna)
        // Aqui usamos Mutexes/CondVars (implementado no thread_pool.c)
        thread_pool_dispatch(pool, client_fd);
    }

    printf("Worker %d a encerrar...\n", worker_id);
    destroy_thread_pool(pool);
    // Não destruímos a SHM nem Semáforos aqui, isso é tarefa do Master!
    // Apenas fechamos o que abrimos (munmap/sem_close seria o ideal aqui)
    exit(0);
}