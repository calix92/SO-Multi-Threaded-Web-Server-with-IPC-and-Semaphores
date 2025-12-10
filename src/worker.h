#ifndef WORKER_H
#define WORKER_H
#include "config.h"

void worker_main(int worker_id, int server_socket, server_config_t* config);

#endif