#ifndef MASTER_H
#define MASTER_H

#include "config.h"

void master_run(server_config_t *config);

void worker_main(int worker_id, int server_socket);

#endif
