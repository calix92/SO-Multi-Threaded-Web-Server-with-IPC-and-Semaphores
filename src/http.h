// src/http.h
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
    char host[128];
    long range_start;
    long range_end;
    int connection_close;
} http_request_t;


// =========================
// HTTP Request Parser
// =========================

int parse_http_request(const char* buffer, http_request_t* req);


// =========================
// HTTP Response Builder
// =========================

// Agora aceita o flag 'keep_alive'
void send_http_response(int fd,
                        int status,
                        const char* status_msg,
                        const char* content_type,
                        const char* body,
                        size_t body_len,
                        int keep_alive);

void send_http_partial_response(int fd, const char* content_type, const char* body, 
                                size_t chunk_size, long start, long end, long total_size, int keep_alive);

#endif