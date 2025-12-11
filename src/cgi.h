// src/cgi.h
#ifndef CGI_H
#define CGI_H

// Executa um script e envia a resposta ao cliente
// Retorna o código de estado HTTP (200 ou 500)
int handle_cgi_request(int client_fd, const char* script_path);

#endif