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
    req->range_start = -1;
    req->range_end = -1;
    req->connection_close = 0;

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
            
        }
        else if (strncasecmp(current, "Range:", 6) == 0) {
            char* val_start = current + 6;
            // Procura "bytes="
            char* bytes_prefix = strstr(val_start, "bytes=");
            if (bytes_prefix) {
                // Tenta ler "bytes=START-END"
                if (sscanf(bytes_prefix, "bytes=%ld-%ld", &req->range_start, &req->range_end) != 2) {
                     // Se falhar, tenta ler "bytes=START-" (até ao fim)
                     sscanf(bytes_prefix, "bytes=%ld-", &req->range_start);
                     req->range_end = -1; // -1 indica até ao fim do ficheiro
                }
            }
        }

        else if (strncasecmp(current, "Connection:", 11) == 0) {
            char* val_start = current + 11;
            while (*val_start == ' ') val_start++;
            if (strncasecmp(val_start, "close", 5) == 0) {
                req->connection_close = 1;
            }
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

    // Enviar header
    if (send(fd, header, header_len, 0) < 0) return;

    // Enviar corpo
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

void send_http_partial_response(int fd, const char* content_type, const char* body, 
                                size_t chunk_size, long start, long end, long total_size, int keep_alive)
{
    char header[4096];
    const char* conn_header = keep_alive ? "keep-alive" : "close";

    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Range: bytes %ld-%ld/%ld\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: %s\r\n"
        "\r\n",
        content_type,
        chunk_size,
        start, end, total_size,
        conn_header
    );

    if (send(fd, header, header_len, 0) < 0) return;
    if (body && chunk_size > 0) {
        send(fd, body, chunk_size, 0);
    }
}