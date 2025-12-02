// logger.c
#include "logger.h"
#include <stdio.h>
#include <time.h>

void log_request(sem_t* log_sem, const char* client_ip,
                 const char* method, const char* path,
                 int status, size_t bytes) {
    // TODO: Implementar log thread-safe
    printf("LOG: %s %s %s %d %zu\n", client_ip, method, path, status, bytes);
}