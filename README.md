# ConcurrentHTTP Server - SO Project

**Sistemas Operativos (40381-SO) | Ano Letivo 2025/2026 | Universidade de Aveiro**

Um servidor web HTTP/1.1 de alto desempenho implementado em C, com arquitetura híbrida multi-processo e multi-thread, utilizando mecanismos avançados de sincronização POSIX (IPC) para gestão segura de recursos partilhados.

---

## Autores

* **David Cálix** (NMec: 125043) - dcalix@ua.pt
* **Diogo Ruivo** (NMec: 126498) - diogo.ruivo@ua.pt
* **Turma:** P2 | **Grupo:** G7

---

## Índice

- [Visão Geral](#-visão-geral)
- [Principais Funcionalidades](#-principais-funcionalidades)
- [Arquitetura do Sistema](#-arquitetura-do-sistema)
- [Compilação e Execução](#️-compilação-e-execução)
- [Configuração](#️-configuração)
- [Mecanismos de Sincronização](#-mecanismos-de-sincronização)
- [Testes e Validação](#-testes-e-validação)
- [Estrutura do Projeto](#-estrutura-do-projeto)
- [Funcionalidades Bónus](#-funcionalidades-bónus)
- [Resolução de Problemas](#-resolução-de-problemas)

---

## Visão Geral

O **ConcurrentHTTP Server** é um servidor web concorrente capaz de processar milhares de conexões simultâneas com alta eficiência. Implementa o padrão arquitetural **Master-Worker** com **Thread Pools**, onde:

- Um **processo mestre** orquestra a infraestrutura e monitoriza estatísticas
- Múltiplos **processos worker** (criados via `fork()`) aceitam conexões
- Cada worker mantém uma **pool de threads** para processamento paralelo de pedidos

O projeto foca-se na **gestão segura de recursos partilhados** e na **prevenção de race conditions** através de primitivas de sincronização POSIX, incluindo semáforos, mutexes e reader-writer locks.

---

## Principais Funcionalidades

### Core Features

| Funcionalidade | Descrição |
|----------------|-----------|
| **HTTP/1.1 Compliant** | Suporte aos métodos `GET` e `HEAD` com parsing robusto de headers |
| **Arquitetura Híbrida** | Multi-Processo (`fork`) + Multi-Thread (`pthreads`) para máxima concorrência |
| **Cache LRU Thread-Safe** | Cache em memória com política *Least Recently Used*, protegida por `pthread_rwlock_t` |
| **Logging Atómico** | Registo de acessos no formato *Apache Combined* com sincronização via semáforos |
| **Estatísticas em Tempo Real** | Monitorização de pedidos, bytes transferidos, erros e cache hits via memória partilhada |
| **Graceful Shutdown** | Encerramento limpo com libertação de todos os recursos (memória, sockets, semáforos) |
| **Error Handling** | Páginas de erro personalizadas (404, 403, 500) em HTML |

### Bónus Features

- **Dashboard Web (`/stats`)**: Interface HTML com atualização automática das estatísticas do servidor
- **Virtual Hosts (VHosts)**: Suporte para múltiplos sites baseado no header `Host:`
- **Keep-Alive**: Conexões persistentes HTTP/1.1 para reduzir overhead
- **Range Requests**: Suporte a pedidos parciais (HTTP 206) para download resumível
- **CGI Support**: Execução de scripts Python com output dinâmico

---

## Arquitetura do Sistema

### Modelo de Processos

```
┌─────────────────────────────────────────────────────┐
│                   MASTER PROCESS                    │
│  - Inicializa Memória Partilhada (SHM)              │
│  - Cria Semáforos (IPC)                             │
│  - Cria Socket de Escuta (bind/listen)              │
│  - Fork de N Workers                                │
│  - Monitoriza Estatísticas (loop infinito)          │
└──────────────────┬──────────────────────────────────┘
                   │ fork()
        ┌──────────┼──────────┬──────────┐
        ▼          ▼          ▼          ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
   │ WORKER  │ │ WORKER  │ │ WORKER  │ │ WORKER  │
   │   #1    │ │   #2    │ │   #3    │ │   #4    │
   └────┬────┘ └────┬────┘ └────┬────┘ └────|────┘
        │           │           │           │
   [Thread Pool] [Thread Pool] [Thread Pool] [Thread Pool]
    (10 threads)  (10 threads)  (10 threads)  (10 threads)
```

### Fluxo de Processamento de Pedidos

```
1. Cliente → TCP Connect → Socket (porta 8080)
2. Worker (semáforo) → accept() → [Mutex Accept]
3. Worker → Dispatch para Thread Pool
4. Thread → Parse HTTP Request
5. Thread → Consulta Cache (rwlock)
6. Thread → [HIT] Responde direto | [MISS] Lê disco + Guarda cache
7. Thread → Atualiza Estatísticas (semáforo)
8. Thread → Escreve Log (semáforo)
9. Thread → Envia Resposta HTTP
10. [Keep-Alive?] Loop back to step 4 | [Close] Fecha socket
```

---

## Compilação e Execução

### Pré-requisitos

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential gcc make apache2-utils valgrind

# Verificar instalação
gcc --version
make --version
ab -V  # Apache Bench (para testes de carga)
```

### Comandos Disponíveis

```bash
# Compilar o projeto
make
# ou
make all

# Limpar ficheiros compilados
make clean

# Iniciar o servidor (com limpeza de recursos IPC)
make run

# Executar suite completa de testes
make test
```

### Execução Manual

```bash
# 1. Compilar
make

# 2. Limpar recursos IPC antigos (importante!)
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null

# 3. Iniciar servidor
./server

# 4. Aceder no browser
# http://localhost:8080/
```

---

## Configuração

O ficheiro `server.conf` controla todos os parâmetros do servidor:

| Parâmetro | Valor Padrão | Descrição |
|-----------|--------------|-----------|
| `PORT` | `8080` | Porta TCP onde o servidor escuta conexões |
| `DOCUMENT_ROOT` | `./www` | Diretoria raiz dos ficheiros estáticos (HTML/CSS/JS) |
| `NUM_WORKERS` | `4` | Número de processos worker (recomendado: nº de cores CPU) |
| `THREADS_PER_WORKER` | `10` | Número de threads por worker (ajustar conforme carga) |
| `MAX_QUEUE_SIZE` | `100` | Tamanho máximo da fila de conexões pendentes |
| `CACHE_SIZE_MB` | `10` | Tamanho máximo da cache LRU em memória (MB) |
| `LOG_FILE` | `access.log` | Caminho para o ficheiro de logs de acessos |
| `TIMEOUT_SECONDS` | `30` | Intervalo de atualização das estatísticas no Master |

### Configuração de Virtual Hosts (Bónus)

```ini
# server.conf
VHOST_site1.local=./www/site1
VHOST_site2.local=./www/site2
```

**Teste com curl:**
```bash
curl -H "Host: site1.local" http://localhost:8080/index.html
```

---

## Mecanismos de Sincronização

O projeto implementa **sincronização defensiva** para garantir a integridade dos dados em ambiente concorrente:

### Tabela de Proteção de Recursos

| Recurso Partilhado | Mecanismo | Tipo | Descrição |
|--------------------|-----------|------|-----------|
| **Accept Socket** | `sem_t *queue_mutex` | Semáforo POSIX | Serializa `accept()` entre workers (evita *thundering herd*) |
| **Memória Partilhada (Stats)** | `sem_t *stats_mutex` | Semáforo POSIX | Protege contadores globais (`total_requests`, `bytes_transferred`, etc.) |
| **Ficheiro de Log** | `sem_t *log_mutex` | Semáforo POSIX | Garante escrita atómica no `access.log` (linhas não se misturam) |
| **Cache LRU** | `pthread_rwlock_t` | RW Lock | Permite múltiplas leituras simultâneas, escrita exclusiva |
| **Fila da Thread Pool** | `pthread_mutex_t` + `pthread_cond_t` | Mutex + Condition Variable | Sincroniza produção/consumo de tarefas |

### Diagrama de Exclusão Mútua no Accept

```
Worker 1: [WAIT] sem_wait(queue_mutex) → accept() → sem_post(queue_mutex)
Worker 2:        [BLOCKED...]            → [WAIT]   → accept() → sem_post()
Worker 3:                                 [BLOCKED...] → [WAIT]  → accept()
```

**Vantagem:** Distribuição uniforme de carga e eliminação do *thundering herd problem*.

---

## Testes e Validação

O projeto inclui uma **suite completa de testes automatizados**:

### Execução Rápida

```bash
# Suite completa (recomendado)
bash tests/run_all_tests.sh

# Testes individuais
bash tests/test_functional.sh   # Funcionalidade HTTP
bash tests/test_load.sh          # Carga com Apache Bench
bash tests/test_sync.sh          # Race conditions (Helgrind)
bash tests/test_memory.sh        # Memory leaks (Valgrind)
bash tests/test_bonus.sh         # Funcionalidades bónus
```

### Categorias de Testes

#### 1. Testes Funcionais (`test_functional.sh`)
- Métodos HTTP (GET, HEAD)
- Content-Type correto (HTML, CSS, JS)
- Códigos de estado (200, 404, 403, 500)
- Páginas de erro personalizadas
- Cache hit/miss
- Formato do `access.log`

#### 2. Testes de Carga (`test_load.sh`)
```bash
# Warm-up: 100 pedidos, 10 concorrentes
ab -n 100 -c 10 http://localhost:8080/index.html

# Stress: 10,000 pedidos, 100 concorrentes
ab -n 10000 -c 100 http://localhost:8080/index.html

# Keep-Alive
ab -n 1000 -c 20 -k http://localhost:8080/index.html
```

**Métricas Analisadas:**
- Requests per Second (RPS)
- Taxa de falhas
- Tempo médio de resposta
- Latência (percentis 50/95/99)

#### 3. Testes de Sincronização (`test_sync.sh`)
Utiliza **Valgrind Helgrind** para detetar:
- Race conditions
- Lock order violations
- Deadlocks

```bash
valgrind --tool=helgrind ./server
```

#### 4. Testes de Memória (`test_memory.sh`)
Utiliza **Valgrind Memcheck** para detetar:
- Memory leaks (definitely/indirectly lost)
- Invalid reads/writes
- Use of uninitialized values

```bash
valgrind --leak-check=full ./server
```

#### 5. Testes de Concorrência Programáticos (`test_concurrent.c`)
Programa em C que:
- Lança 50 threads, cada uma fazendo 20 pedidos
- Mede throughput e latência
- Valida taxa de sucesso (>95%)

```bash
gcc tests/test_concurrent.c -o test_concurrent -lpthread
./server &  # Terminal 1
./test_concurrent  # Terminal 2
```

---

## Estrutura do Projeto

```
.
├── Makefile                # Build system
├── README.md               # Este ficheiro
├── server.conf             # Configuração do servidor
├── src/
│   ├── main.c              # Entry point
│   ├── master.c/h          # Processo Master
│   ├── worker.c/h          # Processos Worker
│   ├── thread_pool.c/h     # Gestão de threads
│   ├── http.c/h            # Parser e builder HTTP
│   ├── cache.c/h           # Cache LRU thread-safe
│   ├── shared_mem.c/h      # Memória partilhada (SHM)
│   ├── semaphores.c/h      # Gestão de semáforos
│   ├── stats.c/h           # Estatísticas e dashboard
│   ├── logger.c/h          # Logging atómico
│   ├── config.c/h          # Parser do server.conf
│   └── cgi.c/h             # Suporte CGI (Bónus)
├── www/
│   ├── index.html          # Página principal
│   ├── style.css           # Estilos
│   ├── script.js           # JavaScript
│   ├── errors/
│   │   ├── 404.html        # Página 404
│   │   ├── 403.html        # Página 403
│   │   └── 500.html        # Página 500
│   ├── site1/              # VHost 1 (Bónus)
│   └── site2/              # VHost 2 (Bónus)
├── tests/
│   ├── run_all_tests.sh    # Suite mestre
│   ├── test_functional.sh  # Testes HTTP
│   ├── test_load.sh        # Apache Bench
│   ├── test_sync.sh        # Helgrind
│   ├── test_memory.sh      # Valgrind
│   ├── test_bonus.sh       # Funcionalidades bónus
│   └── test_concurrent.c   # Testes programáticos
└── obj/                    # Ficheiros .o (gerado)
```

---

## Funcionalidades Bónus

### 1. Dashboard Web (`/stats`)
Interface HTML com atualização automática (refresh 3s) mostrando:
- Uptime do servidor
- Total de pedidos processados
- Bytes transferidos
- Conexões ativas
- Cache hit rate
- Distribuição de códigos HTTP (200, 404, 500)

**Acesso:** `http://localhost:8080/stats`

### 2. Virtual Hosts
Serve diferentes sites baseado no header `Host:` do pedido HTTP.

**Configuração:**
```ini
# server.conf
VHOST_example.com=./www/example
VHOST_test.local=./www/test
```

**Teste:**
```bash
curl -H "Host: example.com" http://localhost:8080/
```

### 3. Keep-Alive (Persistent Connections)
Mantém a conexão TCP aberta para múltiplos pedidos, reduzindo overhead de handshake.

**Comportamento:**
- HTTP/1.1: Keep-Alive por padrão (timeout de 5s)
- HTTP/1.0: Close por padrão
- Header `Connection: close` sempre respeitado

### 4. Range Requests (HTTP 206)
Suporte a downloads resumíveis e streaming.

**Exemplo:**
```bash
# Download dos primeiros 100 bytes
curl -H "Range: bytes=0-99" http://localhost:8080/file.pdf

# Download do byte 1000 até ao fim
curl -H "Range: bytes=1000-" http://localhost:8080/file.pdf
```

**Resposta:**
```http
HTTP/1.1 206 Partial Content
Content-Range: bytes 0-99/5000
Content-Length: 100
```

### 5. CGI Support (Python)
Executa scripts Python e retorna output dinâmico.

**Exemplo:**
```python
# www/hello.py
print("<h1>Hello from Python!</h1>")
print("<p>Current time: {}</p>".format(__import__('time').ctime()))
```

**Acesso:** `http://localhost:8080/hello.py`

**Implementação:**
- Deteta ficheiros `.py`
- Cria pipe para STDOUT
- Executa com `execlp("python3", ...)`
- Captura output e envia como HTML

---

## Resolução de Problemas

### Problema: "Address already in use"
```bash
# Solução: Limpar processos antigos
pkill -9 server
rm -f /dev/shm/ws_* /dev/shm/sem.ws_*

# Ou usar o make run (faz isto automaticamente)
make run
```

### Problema: "Valgrind detecta leaks em pthread_create"
**Normal!** São leaks internos da glibc. Use os ficheiros `.supp` fornecidos:
```bash
valgrind --suppressions=valgrind.supp ./server
```

### Problema: Testes de carga bloqueiam (`ab` fica pendurado)
Verifique:
1. Keep-Alive está configurado? (Pode causar timeouts)
2. Thread Pool tem threads suficientes? (Aumentar `THREADS_PER_WORKER`)
3. Helgrind está ativo? (Reduz performance ~20x, normal em testes de sincronização)

### Problema: "Cache não acelera pedidos"
A cache só funciona para:
- Ficheiros **< 1MB** (ver `cache.c`)
- Pedidos **sem Range** (cache skip em pedidos parciais)
- Segundo acesso ao **mesmo ficheiro**

---

## Estatísticas de Performance

### Ambiente de Teste
- **CPU:** Intel i5-8250U (4 cores, 8 threads)
- **RAM:** 8 GB
- **OS:** Ubuntu 22.04 LTS
- **Config:** 4 workers, 10 threads/worker

### Resultados (Apache Bench)

| Teste | Requests | Concorrência |     RPS      | Taxa Sucesso |
|-------|----------|--------------|--------------|--------------|
| Leve  |     100  |      10      | 8,223 req/s  |    100%      |
| Média |   1,000  |      50      | 15,132 req/s |    100%      |
| Pesada|  10,000  |     100      | 29,503 req/s |    100%      |

 Destaque: O servidor atinge ~30,000 pedidos por segundo sob carga pesada (10k requests, 100 concurrent), demonstrando excelente escalabilidade da arquitetura híbrida multi-processo/multi-thread.

### Cache Hit Rate
- **Warm cache:** 85-92% hits
- **Cold cache:** 0% (primeiro acesso)
- **Mixed workload:** 60-75% hits

---

## Referências

- [RFC 7230 - HTTP/1.1 Message Syntax](https://tools.ietf.org/html/rfc7230)
- [POSIX Threads Programming](https://hpc-tutorials.llnl.gov/posix/)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [Advanced Programming in the UNIX Environment (Stevens)](https://www.apuebook.com/)

---

## Notas Finais

Este projeto demonstra a aplicação prática de conceitos fundamentais de Sistemas Operativos:
- Gestão de processos e threads
- Sincronização e exclusão mútua
- Inter-Process Communication (IPC)
- Programação de sockets
- Gestão de memória e recursos

**Desenvolvido como projeto académico para a UC de Sistemas Operativos (40381-SO), DETI-UA, 2025/2026.**

---

## Licença

Este projeto é desenvolvido exclusivamente para fins académicos na Universidade de Aveiro. Todos os direitos reservados aos autores.