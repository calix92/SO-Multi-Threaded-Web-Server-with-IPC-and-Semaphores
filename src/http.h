// http.h
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>   // size_t

// =========================
// HTTP Request Structure
// =========================

typedef struct {
    char method[16];
    char path[512];
    char version[16];
} http_request_t;


// =========================
// HTTP Request Parser
// =========================

// Parsea a primeira linha da request HTTP.
// Ex: "GET /index.html HTTP/1.1"
// Retorna 0 se OK, -1 se inválido.


int parse_http_request(const char* buffer, http_request_t* req);


// =========================
// HTTP Response Builder
// =========================
//
// Constrói e envia a resposta HTTP completa.
// Inclui headers + body (caso exista).
//

void send_http_response(int fd,
                        int status,
                        const char* status_msg,
                        const char* content_type,
                        const char* body,
                        size_t body_len);

#endif
