// src/logger.c - COM ROTAÇÃO DE LOGS
#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h> 

#define MAX_LOG_SIZE (10 * 1024 * 1024) // 10 MB

void log_request(sem_t* log_sem, const char* client_ip,
                 const char* method, const char* path,
                 int status, size_t bytes) {
    
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", &tm_info);

    // 1. ADQUIRIR O SEMÁFORO
    sem_wait(log_sem);

    // 2. VERIFICAR ROTAÇÃO
    FILE* check_file = fopen("access.log", "r");
    if (check_file) {
        fseek(check_file, 0, SEEK_END);
        long filesize = ftell(check_file);
        fclose(check_file);

        if (filesize >= MAX_LOG_SIZE) {
            // Rotação simples: access.log -> access.log.1 (sobrescreve o antigo .1)
            rename("access.log", "access.log.1");
        }
    }

    // 3. ESCREVER LOG
    FILE* log_file = fopen("access.log", "a");
    if (log_file) {
        fprintf(log_file, "%s - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                client_ip, timestamp, method, path, status, bytes);
        fflush(log_file); 
        fclose(log_file);
    }
    
    // 4. LIBERTAR O SEMÁFORO
    sem_post(log_sem);
}