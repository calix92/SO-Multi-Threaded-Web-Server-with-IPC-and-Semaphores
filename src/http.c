// src/http.c
#include <sys/socket.h>
#include "http.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h> 
#include <strings.h>

// =========================
// 6. HTTP Request Parser
// =========================

int parse_http_request(const char* buffer, http_request_t* req) {
    // 1. Limpar o host por defeito
    req->host[0] = '\0';

    // 2. Parse da primeira linha (Método, Path, Versão)
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;

    char first_line[1024];
    size_t len = line_end - buffer;
    if (len >= sizeof(first_line)) len = sizeof(first_line) - 1;
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';

    if (sscanf(first_line, "%15s %511s %15s", req->method, req->path, req->version) != 3) {
        return -1;
    }

    // 3. Loop para encontrar o cabeçalho "Host:"
    char* current = line_end + 2; // Saltar o primeiro \r\n
    while (current && *current) {
        char* next_line = strstr(current, "\r\n");
        if (!next_line) break;

        // Verificar se a linha começa por "Host:"
        if (strncasecmp(current, "Host:", 5) == 0) {
            char* val_start = current + 5;
            while (*val_start == ' ') val_start++; // Saltar espaços
            
            size_t val_len = next_line - val_start;
            if (val_len >= sizeof(req->host)) val_len = sizeof(req->host) - 1;
            
            strncpy(req->host, val_start, val_len);
            req->host[val_len] = '\0';
            
            // Remover porta se existir (ex: localhost:8080 -> localhost)
            char* port_sep = strchr(req->host, ':');
            if (port_sep) *port_sep = '\0';
            
            break; // Já temos o host, podemos sair
        }
        current = next_line + 2;
    }
    return 0;
}


// =========================
// 7. HTTP Response Builder
// =========================

void send_http_response(int fd,
                        int status,
                        const char* status_msg,
                        const char* content_type,
                        const char* body,
                        size_t body_len,
                        int keep_alive)
{
    char header[4096];

    // Decide se fecha ou mantém
    const char* conn_header = keep_alive ? "keep-alive" : "close";

    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status,
        status_msg,
        content_type,
        body_len,
        conn_header
    );

    // Enviar header (verificar erros seria ideal, mas simplificamos)
    if (send(fd, header, header_len, 0) < 0) return;

    // Enviar corpo
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}