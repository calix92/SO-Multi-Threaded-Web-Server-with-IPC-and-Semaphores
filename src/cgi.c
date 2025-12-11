// src/cgi.c
#define _POSIX_C_SOURCE 200809L
#include "cgi.h"
#include "http.h" // Para send_http_response
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

int handle_cgi_request(int client_fd, const char* script_path) {
    int pipefd[2];
    
    // 1. Criar Pipe
    if (pipe(pipefd) == -1) {
        perror("CGI pipe error");
        return 500;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("CGI fork error");
        return 500;
    }

    if (pid == 0) {
        // --- PROCESSO FILHO ---
        close(pipefd[0]); // Fecha leitura
        
        // Redirecionar STDOUT para o pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Executar o script Python
        // Assume que o python3 está no PATH
        execlp("python3", "python3", script_path, NULL);
        
        // Se execlp falhar:
        perror("CGI exec error");
        exit(1);
    } 
    else {
        // --- PROCESSO PAI ---
        close(pipefd[1]); // Fecha escrita

        // Ler o output do script
        char* output_buffer = malloc(65536);
        if (!output_buffer) {
            close(pipefd[0]);
            waitpid(pid, NULL, 0);
            return 500;
        }

        int total_read = 0;
        int n;
        while ((n = read(pipefd[0], output_buffer + total_read, 65536 - total_read)) > 0) {
            total_read += n;
            if (total_read >= 65536) break; // Buffer cheio
        }

        close(pipefd[0]);
        
        // Esperar que o filho morra para evitar Zombies!
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // Sucesso: Enviar resposta HTTP com o output do script
            send_http_response(client_fd, 200, "OK", "text/html", output_buffer, total_read, 0);
            free(output_buffer);
            return 200;
        } else {
            // Script falhou
            free(output_buffer);
            return 500;
        }
    }
}