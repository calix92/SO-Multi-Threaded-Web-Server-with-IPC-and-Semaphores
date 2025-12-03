// src/logger.c
#include "logger.h"
#include "config.h" // Necessário para aceder ao nome do log file
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h> 

void log_request(sem_t* log_sem, const char* client_ip,
                 const char* method, const char* path,
                 int status, size_t bytes) {
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    // Formato: [DD/Month/YYYY:HH:MM:SS +0000]
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", tm_info);

    // 1. ADQUIRIR O SEMÁFORO (Exclusão Mútua IPC)
    sem_wait(log_sem);

    FILE* log_file = fopen("access.log", "a"); // Usar o nome do ficheiro (do server.conf)

    if (log_file) {
        // 2. ESCREVER A LINHA NO FORMATO CORRETO (Apache Combined Log Format)
        fprintf(log_file, "%s - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                client_ip, timestamp, method, path, status, bytes);
        
        // 3. GARANTIR ESCRITA IMEDIATA
        fflush(log_file); 
        fclose(log_file);
    }
    
    // 4. LIBERTAR O SEMÁFORO
    sem_post(log_sem);
}