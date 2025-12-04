#!/bin/bash
# tests/test_sync.sh
# Testes de Sincronização com Valgrind Helgrind

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "=========================================="
echo "   TESTES DE SINCRONIZAÇÃO - Helgrind"
echo "=========================================="
echo ""

# Verificar se valgrind está instalado
if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}ERRO: Valgrind não está instalado!${NC}"
    echo "Instale com: sudo apt-get install valgrind"
    exit 1
fi

# Verificar se o binário existe
if [ ! -f "./server" ]; then
    echo -e "${RED}ERRO: Binário ./server não encontrado!${NC}"
    echo "Execute 'make' primeiro!"
    exit 1
fi

echo -e "${YELLOW}⚠ AVISO: Este teste é LENTO (pode demorar 5-10 minutos)${NC}"
echo -e "${YELLOW}⚠ O Helgrind adiciona overhead significativo${NC}"
echo ""
echo "Pressione ENTER para continuar ou Ctrl+C para cancelar..."
read

# Limpar recursos IPC antigos
echo ">> A limpar recursos IPC antigos..."
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true

# ==========================================
# TESTE 1: Race Conditions (Helgrind)
# ==========================================
echo -e "${BLUE}TESTE 1: Deteção de Race Conditions${NC}"
echo "A iniciar servidor com Helgrind..."
echo ""

# Iniciar servidor com Helgrind em background
valgrind --tool=helgrind \
         --log-file=helgrind_output.log \
         --suppressions=/usr/lib/valgrind/default.supp \
         ./server &

SERVER_PID=$!
echo "Servidor iniciado com PID: $SERVER_PID"
echo "Aguardando 5 segundos para o servidor arrancar..."
sleep 5

# Verificar se o servidor está a correr
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ FAIL: Servidor crashou durante arranque com Helgrind!${NC}"
    cat helgrind_output.log
    exit 1
fi

# Gerar tráfego
echo ">> A gerar tráfego para detetar race conditions..."
echo "   Enviando 100 pedidos concorrentes..."

for i in {1..100}; do
    curl -s http://localhost:8080/index.html > /dev/null &
done
wait

echo "   Tráfego enviado. Aguardando 3 segundos..."
sleep 3

# Parar o servidor
echo ">> A parar o servidor..."
kill -SIGINT $SERVER_PID
sleep 3

# Forçar kill se ainda estiver vivo
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "   Forçando encerramento..."
    kill -9 $SERVER_PID
fi
wait $SERVER_PID 2>/dev/null

# Analisar resultados do Helgrind
echo ""
echo "=========================================="
echo "   ANÁLISE DO HELGRIND"
echo "=========================================="

RACE_CONDITIONS=$(grep -c "Possible data race" helgrind_output.log 2>/dev/null || echo 0)
LOCK_ORDER=$(grep -c "lock order violation" helgrind_output.log 2>/dev/null || echo 0)
THREADS_CREATED=$(grep -c "Thread #" helgrind_output.log 2>/dev/null || echo 0)

echo "Threads criadas: $THREADS_CREATED"
echo "Race Conditions detetadas: $RACE_CONDITIONS"
echo "Violações de ordem de locks: $LOCK_ORDER"
echo ""

if [ $RACE_CONDITIONS -eq 0 ] && [ $LOCK_ORDER -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Nenhuma race condition detetada!${NC}"
    HELGRIND_PASS=1
else
    echo -e "${RED}✗ FAIL: Problemas de sincronização detetados!${NC}"
    echo ""
    echo "Primeiras 50 linhas com problemas:"
    grep -A 10 "Possible data race\|lock order" helgrind_output.log | head -50
    HELGRIND_PASS=0
fi

echo ""
echo "Log completo guardado em: helgrind_output.log"
echo ""

# ==========================================
# TESTE 2: Integridade do Log (access.log)
# ==========================================
echo -e "${BLUE}TESTE 2: Integridade do Ficheiro de Log${NC}"
echo "A verificar se o access.log tem linhas corrompidas..."
echo ""

# Limpar log antigo
rm -f access.log

# Iniciar servidor normal
./server &
SERVER_PID=$!
sleep 3

# Gerar tráfego intenso simultâneo
echo ">> A gerar 500 pedidos simultâneos para testar concorrência no log..."
for i in {1..500}; do
    curl -s http://localhost:8080/index.html > /dev/null &
done
wait

# Parar servidor
kill -SIGINT $SERVER_PID
wait $SERVER_PID 2>/dev/null
sleep 2

# Verificar integridade do log
if [ ! -f "access.log" ]; then
    echo -e "${RED}✗ FAIL: access.log não foi criado!${NC}"
    LOG_PASS=0
else
    TOTAL_LINES=$(wc -l < access.log)
    # Formato esperado: IP - [timestamp] "METHOD path" status bytes
    VALID_LINES=$(grep -cE '^\S+ - \[.*\] ".*" [0-9]{3} [0-9]+$' access.log)
    INVALID_LINES=$((TOTAL_LINES - VALID_LINES))
    
    echo "Linhas totais: $TOTAL_LINES"
    echo "Linhas válidas: $VALID_LINES"
    echo "Linhas inválidas/corrompidas: $INVALID_LINES"
    echo ""
    
    if [ $INVALID_LINES -eq 0 ]; then
        echo -e "${GREEN}✓ PASS: Log está íntegro (sem linhas misturadas)!${NC}"
        LOG_PASS=1
    else
        echo -e "${RED}✗ FAIL: $INVALID_LINES linhas corrompidas no log!${NC}"
        echo ""
        echo "Exemplos de linhas inválidas:"
        grep -vE '^\S+ - \[.*\] ".*" [0-9]{3} [0-9]+$' access.log | head -10
        LOG_PASS=0
    fi
fi

echo ""

# ==========================================
# TESTE 3: Stress Test de Longa Duração
# ==========================================
echo -e "${BLUE}TESTE 3: Stress Test (5 minutos sob carga)${NC}"
echo -e "${YELLOW}⚠ Este teste vai correr durante 5 minutos...${NC}"
echo "Pressione Ctrl+C para pular este teste"
echo ""

# Limpar recursos
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true

# Iniciar servidor
./server &
SERVER_PID=$!
sleep 3

echo ">> Servidor a correr. A enviar tráfego contínuo por 5 minutos..."
START_TIME=$(date +%s)
END_TIME=$((START_TIME + 300)) # 5 minutos

REQUEST_COUNT=0
while [ $(date +%s) -lt $END_TIME ]; do
    for i in {1..50}; do
        curl -s http://localhost:8080/index.html > /dev/null &
        ((REQUEST_COUNT++))
    done
    sleep 2
done
wait

echo ""
echo "Total de pedidos enviados: $REQUEST_COUNT"

# Parar servidor
kill -SIGINT $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Verificar se o servidor não crashou
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Servidor aguentou 5 minutos sob carga!${NC}"
    STRESS_PASS=1
else
    echo -e "${RED}✗ FAIL: Servidor crashou durante o stress test!${NC}"
    STRESS_PASS=0
fi

echo ""

# ==========================================
# RESUMO FINAL
# ==========================================
echo "=========================================="
echo "   RESUMO DOS TESTES DE SINCRONIZAÇÃO"
echo "=========================================="
echo ""

TOTAL_TESTS=3
PASSED_TESTS=$((HELGRIND_PASS + LOG_PASS + STRESS_PASS))

echo "Testes passados: $PASSED_TESTS/$TOTAL_TESTS"
echo ""

if [ $HELGRIND_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Helgrind: Sem race conditions"
else
    echo -e "${RED}✗${NC} Helgrind: Race conditions detetadas"
fi

if [ $LOG_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Log Integrity: Sem corrupção"
else
    echo -e "${RED}✗${NC} Log Integrity: Linhas corrompidas"
fi

if [ $STRESS_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Stress Test: Servidor estável"
else
    echo -e "${RED}✗${NC} Stress Test: Servidor crashou"
fi

echo ""

if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    echo -e "${GREEN}✓✓✓ TODOS OS TESTES DE SINCRONIZAÇÃO PASSARAM!${NC}"
    echo ""
    echo "O servidor está thread-safe e sem race conditions!"
    exit 0
else
    echo -e "${RED}✗✗✗ ALGUNS TESTES FALHARAM!${NC}"
    echo ""
    echo "Verifique:"
    echo "  1. helgrind_output.log para detalhes de race conditions"
    echo "  2. access.log para linhas corrompidas"
    echo "  3. Logs do servidor no terminal"
    exit 1
fi