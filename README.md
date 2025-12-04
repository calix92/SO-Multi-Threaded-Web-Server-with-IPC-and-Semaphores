# ConcurrentHTTP Server - SO Project

**Sistemas Operativos (40381-SO)** **Ano Letivo 2025/2026** **Universidade de Aveiro**

Um servidor web HTTP/1.1 de nível de produção, implementado em C, utilizando uma arquitetura multi-processo e multi-thread com mecanismos avançados de sincronização (IPC).

## Autores

* **Diogo Ruivo** (NMec: 126498) - [diogo.ruivo@ua.pt]
* **David Cálix** (NMec: 125043) - [davidcalix@ua.pt]
* **Turma:** [P1, P2, etc.] | **Grupo:** [G1, G2...]

---

## Visão Geral

Este projeto implementa um servidor web concorrente capaz de lidar com milhares de conexões simultâneas. A arquitetura baseia-se no modelo **Master-Worker**, onde um processo mestre gere processos trabalhadores, e cada trabalhador mantém uma **Thread Pool** para processamento de pedidos.

O foco principal é a gestão eficiente de recursos partilhados e a prevenção de *race conditions* utilizando primitivas de sincronização POSIX.

### Principais Funcionalidades
* ✅ **Arquitetura Híbrida:** Multi-Processo (`fork`) e Multi-Thread (`pthreads`).
* ✅ **HTTP/1.1:** Suporte aos métodos `GET` e `HEAD`.
* ✅ **Cache LRU Thread-Safe:** Cache em memória com política de substituição *Least Recently Used*, protegida por *Reader-Writer Locks* para alta performance.
* ✅ **Logging Atómico:** Registo de acessos (`access.log`) no formato *Apache Combined*, sincronizado entre processos via Semáforos.
* ✅ **Estatísticas Partilhadas:** Monitorização em tempo real (pedidos, bytes, erros) armazenada em Memória Partilhada (SHM).
* ✅ **Páginas de Erro:** Gestão personalizada de erros 404 e 500 (HTML).
* ✅ **Graceful Shutdown:** Encerramento limpo de todos os recursos (memória, sockets, semáforos) ao receber `SIGINT`/`SIGTERM`.

---

## ⚙️ Compilação e Execução

### Pré-requisitos
* Linux (Ubuntu/Debian recomendado)
* GCC Compiler
* Make
* `apache2-utils` (para testes de carga com `ab`)

### Comandos do Makefile

1.  **Compilar o projeto:**
    ```bash
    make
    # ou
    make all
    ```

2.  **Limpar ficheiros compilados:**
    ```bash
    make clean
    ```

3.  **Iniciar o Servidor:**
    ```bash
    make run
    ```
    *(Nota: O comando `make run` inclui uma limpeza preventiva de recursos `/dev/shm` para garantir um arranque limpo).*

4.  **Executar Testes de Carga:**
    ```bash
    make test
    ```

---

## 🛠️ Configuração (`server.conf`)

O comportamento do servidor é definido no ficheiro `server.conf`. As opções disponíveis são:

| Parâmetro | Valor Padrão | Descrição |
| :--- | :--- | :--- |
| `PORT` | 8080 | Porta TCP onde o servidor escuta. |
| `DOCUMENT_ROOT` | ./www | Diretoria raiz dos ficheiros HTML/CSS/JS. |
| `NUM_WORKERS` | 4 | Número de processos trabalhadores (Workers). |
| `THREADS_PER_WORKER` | 10 | Número de threads por Worker. |
| `MAX_QUEUE_SIZE` | 100 | Tamanho da fila de conexões (Semáforo). |
| `CACHE_SIZE_MB` | 10 | Tamanho máximo da cache em memória (MB). |
| `LOG_FILE` | access.log | Caminho para o ficheiro de logs. |
| `TIMEOUT_SECONDS` | 30 | Intervalo de atualização das estatísticas no Master. |

---

## Arquitetura e Sincronização

### 1. Modelo de Processos (Master-Worker)
* **Master:** Inicializa a Memória Partilhada e os Semáforos, cria o *socket* de escuta e faz `fork()` de `NUM_WORKERS` processos. Fica num loop de monitorização a exibir estatísticas.
* **Workers:** Herdam o *socket* do Master. Utilizam um mecanismo de **Exclusão Mútua no Accept** (serialização) para evitar o problema *thundering herd*.
* **Threads:** Cada Worker cria uma `ThreadPool` fixa. As threads consomem conexões aceites e processam o pedido HTTP.

### 2. Sincronização (IPC e Threads)

O sistema utiliza mecanismos rigorosos para garantir a integridade dos dados:

| Recurso Partilhado | Mecanismo de Proteção | Descrição |
| :--- | :--- | :--- |
| **Accept Socket** | `sem_t *queue_mutex` | Semáforo POSIX. Garante que apenas um Worker de cada vez tenta fazer `accept()`, distribuindo a carga uniformemente. |
| **Memória Partilhada (Stats)** | `sem_t *stats_mutex` | Semáforo POSIX. Protege a escrita na `struct shared_data_t` (contadores globais) acessível por todos os processos. |
| **Ficheiro de Log** | `sem_t *log_mutex` | Semáforo POSIX. Garante que a escrita no ficheiro `access.log` é atómica (as linhas não se misturam). |
| **Cache LRU** | `pthread_rwlock_t` | Reader-Writer Lock (Intra-processo). Permite múltiplas leituras simultâneas (`rdlock`) mas escrita exclusiva (`wrlock`) na cache. |
| **Fila da Thread Pool** | `pthread_mutex_t` | Mutex (Intra-processo). Protege a lista ligada de tarefas dentro de cada Worker. |

---

## Testes e Validação

### Teste Funcional
Aceder via browser a `http://localhost:8080/index.html` ou utilizar o curl:
```bash
curl -v http://localhost:8080/index.html