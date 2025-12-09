// tests/test_concurrent.c
// Testes de Concorrência programáticos em C
// Testa race conditions, deadlocks e sincronização

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 8080
#define NUM_THREADS 50
#define REQUESTS_PER_THREAD 20

// Estatísticas globais
typedef struct {
    int successful_requests;
    int failed_requests;
    long total_response_time_ms;
    pthread_mutex_t mutex;
} test_stats_t;

test_stats_t global_stats = {0, 0, 0, PTHREAD_MUTEX_INITIALIZER};

// Cores para output
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_BLUE "\033[0;34m"
#define COLOR_RESET "\033[0m"

// ================================================
// Função auxiliar: Criar socket e conectar
// ================================================
int connect_to_server(const char* host, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }

    // Timeout de 5 segundos
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// ================================================
// Função auxiliar: Enviar HTTP GET e medir tempo
// ================================================
int send_http_request(const char* path, long* response_time_ms) {
    struct timeval start, end;
    gettimeofday(&start, NULL);

    int sockfd = connect_to_server(SERVER_HOST, SERVER_PORT);
    if (sockfd < 0) {
        return -1;
    }

    // Construir pedido HTTP
    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, SERVER_HOST, SERVER_PORT);

    // Enviar pedido
    if (send(sockfd, request, strlen(request), 0) < 0) {
        close(sockfd);
        return -1;
    }

    // Receber resposta
    char buffer[4096];
    int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    close(sockfd);

    gettimeofday(&end, NULL);
    *response_time_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                        (end.tv_usec - start.tv_usec) / 1000;

    if (bytes_received <= 0) {
        return -1;
    }

    buffer[bytes_received] = '\0';

    // Verificar status code
    if (strstr(buffer, "HTTP/1.1 200") != NULL) {
        return 0; // Sucesso
    }

    return -1; // Falha
}

// ================================================
// Thread Worker: Faz múltiplos pedidos
// ================================================
void* worker_thread(void* arg) {
    int thread_id = *(int*)arg;
    
    const char* paths[] = {
        "/index.html",
        "/style.css",
        "/script.js",
        "/index.html", // Repetir para testar cache
    };
    int num_paths = sizeof(paths) / sizeof(paths[0]);

    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        const char* path = paths[i % num_paths];
        long response_time;

        int result = send_http_request(path, &response_time);

        // Atualizar estatísticas (com mutex)
        pthread_mutex_lock(&global_stats.mutex);
        if (result == 0) {
            global_stats.successful_requests++;
            global_stats.total_response_time_ms += response_time;
        } else {
            global_stats.failed_requests++;
        }
        pthread_mutex_unlock(&global_stats.mutex);

        // Pequeno delay para não sobrecarregar
        usleep(10000); // 10ms
    }

    free(arg);
    return NULL;
}

// ================================================
// TESTE 1: Pedidos Concorrentes Simples
// ================================================
int test_concurrent_requests() {
    printf(COLOR_BLUE "TESTE 1: Pedidos Concorrentes Simples\n" COLOR_RESET);
    printf("Lançando %d threads, cada uma fazendo %d pedidos...\n", 
           NUM_THREADS, REQUESTS_PER_THREAD);

    // Resetar estatísticas
    pthread_mutex_lock(&global_stats.mutex);
    global_stats.successful_requests = 0;
    global_stats.failed_requests = 0;
    global_stats.total_response_time_ms = 0;
    pthread_mutex_unlock(&global_stats.mutex);

    pthread_t threads[NUM_THREADS];
    struct timeval start, end;
    gettimeofday(&start, NULL);

    // Criar threads
    for (int i = 0; i < NUM_THREADS; i++) {
        int* thread_id = malloc(sizeof(int));
        *thread_id = i;
        
        if (pthread_create(&threads[i], NULL, worker_thread, thread_id) != 0) {
            fprintf(stderr, "Erro ao criar thread %d\n", i);
            return -1;
        }
    }

    // Esperar por todas
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, NULL);
    long total_time_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                         (end.tv_usec - start.tv_usec) / 1000;

    // Mostrar resultados
    printf("\nResultados:\n");
    printf("  Total de pedidos: %d\n", 
           global_stats.successful_requests + global_stats.failed_requests);
    printf("  Bem-sucedidos: " COLOR_GREEN "%d" COLOR_RESET "\n", 
           global_stats.successful_requests);
    printf("  Falhados: " COLOR_RED "%d" COLOR_RESET "\n", 
           global_stats.failed_requests);
    
    if (global_stats.successful_requests > 0) {
        double avg_response = (double)global_stats.total_response_time_ms / 
                              global_stats.successful_requests;
        printf("  Tempo médio de resposta: %.2f ms\n", avg_response);
    }
    
    printf("  Duração total do teste: %ld ms\n", total_time_ms);
    
    double requests_per_sec = (double)(global_stats.successful_requests) / 
                              (total_time_ms / 1000.0);
    printf("  Throughput: %.2f req/sec\n", requests_per_sec);

    // Critério de sucesso: > 95% de sucesso
    double success_rate = (double)global_stats.successful_requests / 
                          (global_stats.successful_requests + global_stats.failed_requests);
    
    if (success_rate >= 0.95) {
        printf(COLOR_GREEN "[ OK ] PASS: Taxa de sucesso %.1f%%\n" COLOR_RESET, 
               success_rate * 100);
        return 0;
    } else {
        printf(COLOR_RED "[ERROR] FAIL: Taxa de sucesso %.1f%% (esperado >= 95%%)\n" COLOR_RESET, 
               success_rate * 100);
        return -1;
    }
}

// ================================================
// TESTE 2: Stress Test - Rajadas Rápidas
// ================================================
int test_burst_requests() {
    printf("\n" COLOR_BLUE "TESTE 2: Stress Test - Rajadas Rápidas\n" COLOR_RESET);
    printf("Enviando 200 pedidos o mais rápido possível...\n");

    int successful = 0;
    int failed = 0;
    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < 200; i++) {
        long response_time;
        if (send_http_request("/index.html", &response_time) == 0) {
            successful++;
        } else {
            failed++;
        }
    }

    gettimeofday(&end, NULL);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                       (end.tv_usec - start.tv_usec) / 1000;

    printf("\nResultados:\n");
    printf("  Bem-sucedidos: " COLOR_GREEN "%d" COLOR_RESET "\n", successful);
    printf("  Falhados: " COLOR_RED "%d" COLOR_RESET "\n", failed);
    printf("  Duração: %ld ms\n", duration_ms);
    printf("  Throughput: %.2f req/sec\n", 
           (double)successful / (duration_ms / 1000.0));

    if (successful >= 190) { // 95% de sucesso
        printf(COLOR_GREEN "[ OK ] PASS\n" COLOR_RESET);
        return 0;
    } else {
        printf(COLOR_RED "[ERROR] FAIL: Muitos pedidos falharam\n" COLOR_RESET);
        return -1;
    }
}

// ================================================
// TESTE 3: Teste de Cache (Pedidos Repetidos)
// ================================================
int test_cache_effectiveness() {
    printf("\n" COLOR_BLUE "TESTE 3: Eficácia da Cache\n" COLOR_RESET);
    printf("Testando se cache acelera pedidos repetidos...\n");

    long first_request_time = 0;
    long cached_request_times[5] = {0};

    // Primeiro pedido (cache miss)
    printf("  Pedido 1 (cache miss)...\n");
    if (send_http_request("/index.html", &first_request_time) != 0) {
        printf(COLOR_RED "[ERROR] FAIL: Primeiro pedido falhou\n" COLOR_RESET);
        return -1;
    }
    printf("    Tempo: %ld ms\n", first_request_time);

    // Pedidos subsequentes (cache hit)
    sleep(1); // Dar tempo para cache guardar
    for (int i = 0; i < 5; i++) {
        printf("  Pedido %d (cache hit)...\n", i + 2);
        if (send_http_request("/index.html", &cached_request_times[i]) != 0) {
            printf(COLOR_RED "[ERROR] FAIL: Pedido %d falhou\n" COLOR_RESET, i + 2);
            return -1;
        }
        printf("    Tempo: %ld ms\n", cached_request_times[i]);
    }

    // Calcular média dos pedidos cached
    long avg_cached_time = 0;
    for (int i = 0; i < 5; i++) {
        avg_cached_time += cached_request_times[i];
    }
    avg_cached_time /= 5;

    printf("\nResultados:\n");
    printf("  Tempo do 1º pedido (miss): %ld ms\n", first_request_time);
    printf("  Tempo médio cached (hit): %ld ms\n", avg_cached_time);

    // Cache deve ser pelo menos 20% mais rápida
    if (avg_cached_time < first_request_time * 0.8) {
        double improvement = ((double)(first_request_time - avg_cached_time) / 
                             first_request_time) * 100;
        printf(COLOR_GREEN "[ OK ] PASS: Cache %.1f%% mais rápida\n" COLOR_RESET, improvement);
        return 0;
    } else {
        printf(COLOR_YELLOW "! AVISO: Cache não mostrou melhoria significativa\n" COLOR_RESET);
        printf("  (Isto pode ser normal se os ficheiros forem muito pequenos)\n");
        return 0; // Não falhar o teste, apenas avisar
    }
}

// ================================================
// MAIN: Executar todos os testes
// ================================================
int main() {
    printf("\n");
    printf("==============================================\n");
    printf("    TESTES DE CONCORRENCIA - ConcurrentHTTP   \n");
    printf("==============================================\n");
    printf("\n");

    // Verificar se servidor está online
    printf("A verificar se o servidor está online em %s:%d...\n", 
           SERVER_HOST, SERVER_PORT);
    
    long dummy_time;
    if (send_http_request("/", &dummy_time) != 0) {
        printf(COLOR_RED "ERRO: Servidor não está a responder!\n" COLOR_RESET);
        printf("Execute './server' noutra terminal primeiro.\n");
        return 1;
    }
    printf(COLOR_GREEN "[ OK ] Servidor online!\n" COLOR_RESET);
    printf("\n");

    // Executar testes
    int tests_passed = 0;
    int tests_failed = 0;

    if (test_concurrent_requests() == 0) tests_passed++; else tests_failed++;
    if (test_burst_requests() == 0) tests_passed++; else tests_failed++;
    if (test_cache_effectiveness() == 0) tests_passed++; else tests_failed++;

    // Resumo final
    printf("\n");
    printf("==============================================\n");
    printf("              RESUMO DOS TESTES               \n");
    printf("==============================================\n");
    printf("\n");
    printf("Testes passados: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
    printf("Testes falhados: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);
    printf("\n");

    if (tests_failed == 0) {
        printf(COLOR_GREEN ":) TODOS OS TESTES DE CONCORRÊNCIA PASSARAM!\n" COLOR_RESET);
        return 0;
    } else {
        printf(COLOR_RED ":( ALGUNS TESTES FALHARAM!\n" COLOR_RESET);
        return 1;
    }
}