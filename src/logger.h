// logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <semaphore.h>

void log_request(sem_t* log_sem, const char* client_ip, 
                 const char* method, const char* path, 
                 int status, size_t bytes);

#endif