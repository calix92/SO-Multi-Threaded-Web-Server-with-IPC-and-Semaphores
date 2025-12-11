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

    // Master executa no pai; workers fazem fork dentro de master_init()
    master_run(&config);

    return 0;
}
