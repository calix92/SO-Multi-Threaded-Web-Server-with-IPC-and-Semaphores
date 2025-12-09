// src/http.c
#include <sys/socket.h>
#include "http.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h> 

// =========================
// 6. HTTP Request Parser
// =========================

int parse_http_request(const char* buffer, http_request_t* req) {
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return -1;

    char first_line[1024];
    size_t len = line_end - buffer;
    if (len >= sizeof(first_line)) len = sizeof(first_line) - 1;

    strncpy(first_line, buffer, len);
    first_line[len] = '\0';

    if (sscanf(first_line, "%15s %511s %15s",
               req->method,
               req->path,
               req->version) != 3)
    {
        return -1;
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
                        int keep_alive) // <--- NOVO PARÂMETRO
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
        "Connection: %s\r\n"  // <--- Dinâmico
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