#include <stdio.h>
#include <unistd.h>
#include "master.h"
#include <string.h>
#include "worker.h"
#include "config.h"

int main(int argc, char *argv[]) {
    server_config_t config;
    memset(&config, 0, sizeof(config));
    if (load_config("server.conf", &config) != 0) {
        printf("Erro ao carregar server.conf\n");
        return 1;
    }

    // Master runs in parent; workers fork inside master_init()
    master_run(&config);

    return 0;
}
