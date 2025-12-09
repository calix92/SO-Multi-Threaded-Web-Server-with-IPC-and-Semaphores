#!/bin/bash
# tests/test_sync.sh - VERSÃO CORRIGIDA
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

if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}ERRO: Valgrind não está instalado!${NC}"
    exit 1
fi

if [ ! -f "./server" ]; then
    echo -e "${RED}ERRO: Binário ./server não encontrado!${NC}"
    exit 1
fi

echo -e "${YELLOW}⚠ AVISO: Este teste é LENTO (pode demorar 5-10 minutos)${NC}"
echo ""

# ✅ Criar ficheiro de suppressions
cat > valgrind_helgrind.supp << 'EOF'
{
   glibc_timezone_all
   Helgrind:Race
   ...
   fun:__tz*
}
{
   glibc_localtime_all
   Helgrind:Race
   ...
   fun:localtime_r
}
{
   pthread_create_all
   Helgrind:Race
   ...
   fun:pthread_create*
}
EOF

rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true

echo -e "${BLUE}TESTE 1: Deteção de Race Conditions${NC}"
echo "A iniciar servidor com Helgrind..."
echo ""

# ✅ Adicionar suppressions ao comando
valgrind --tool=helgrind \
         --suppressions=valgrind_helgrind.supp \
         --log-file=helgrind_output.log \
         ./server &

SERVER_PID=$!
echo "Servidor iniciado com PID: $SERVER_PID"
echo "Aguardando 8 segundos para o servidor arrancar..."
sleep 8

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ FAIL: Servidor crashou!${NC}"
    cat helgrind_output.log
    exit 1
fi

echo ">> A gerar tráfego..."
for i in {1..100}; do
    curl -s http://localhost:8080/index.html > /dev/null &
done
wait

echo "   Aguardando 5 segundos..."
sleep 5

echo ">> A parar o servidor..."
kill -SIGINT $SERVER_PID
sleep 5

if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID
fi
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "   ANÁLISE DO HELGRIND"
echo "=========================================="

# ✅ Filtrar apenas race conditions REAIS (ignorar glibc)
RACE_CONDITIONS=$(grep "Possible data race" helgrind_output.log | \
                  grep -v "__tz\|localtime\|pthread_create\|_dl_init" | \
                  wc -l)
LOCK_ORDER=$(grep -c "lock order violation" helgrind_output.log 2>/dev/null || echo 0)
THREADS_CREATED=$(grep -c "Thread #" helgrind_output.log 2>/dev/null || echo 0)

echo "Threads criadas: $THREADS_CREATED"
echo "Race Conditions (excluindo glibc): $RACE_CONDITIONS"
echo "Violações de ordem de locks: $LOCK_ORDER"
echo ""

if [ $RACE_CONDITIONS -eq 0 ] && [ $LOCK_ORDER -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Nenhuma race condition real detetada!${NC}"
    HELGRIND_PASS=1
else
    echo -e "${RED}✗ FAIL: $RACE_CONDITIONS race conditions detetadas!${NC}"
    echo ""
    echo "Detalhes (excluindo glibc):"
    grep -A 10 "Possible data race" helgrind_output.log | \
    grep -v "__tz\|localtime\|pthread_create" | head -50
    HELGRIND_PASS=0
fi

echo ""
echo "Log completo: helgrind_output.log"
echo ""

# ==========================================
# TESTE 2: Integridade do Log
# ==========================================
echo -e "${BLUE}TESTE 2: Integridade do Ficheiro de Log${NC}"
echo ""

rm -f access.log
./server &
SERVER_PID=$!
sleep 3

echo ">> A gerar 500 pedidos simultâneos..."
for i in {1..500}; do
    curl -s http://localhost:8080/index.html > /dev/null &
done
wait

kill -SIGINT $SERVER_PID
wait $SERVER_PID 2>/dev/null
sleep 2

if [ ! -f "access.log" ]; then
    echo -e "${RED}✗ FAIL: access.log não foi criado!${NC}"
    LOG_PASS=0
else
    TOTAL_LINES=$(wc -l < access.log)
    VALID_LINES=$(grep -cE '^\S+ - \[.*\] ".*" [0-9]{3} [0-9]+$' access.log)
    INVALID_LINES=$((TOTAL_LINES - VALID_LINES))
    
    echo "Linhas totais: $TOTAL_LINES"
    echo "Linhas válidas: $VALID_LINES"
    echo "Linhas inválidas: $INVALID_LINES"
    echo ""
    
    if [ $INVALID_LINES -eq 0 ]; then
        echo -e "${GREEN}✓ PASS: Log está íntegro!${NC}"
        LOG_PASS=1
    else
        echo -e "${RED}✗ FAIL: $INVALID_LINES linhas corrompidas!${NC}"
        grep -vE '^\S+ - \[.*\] ".*" [0-9]{3} [0-9]+$' access.log | head -10
        LOG_PASS=0
    fi
fi

echo ""

# ==========================================
# RESUMO
# ==========================================
echo "=========================================="
echo "   RESUMO DOS TESTES DE SINCRONIZAÇÃO"
echo "=========================================="
echo ""

PASSED_TESTS=$((HELGRIND_PASS + LOG_PASS))

if [ $HELGRIND_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Helgrind: Sem race conditions reais"
else
    echo -e "${RED}✗${NC} Helgrind: Race conditions detetadas"
fi

if [ $LOG_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Log Integrity: Sem corrupção"
else
    echo -e "${RED}✗${NC} Log Integrity: Linhas corrompidas"
fi

echo ""

if [ $PASSED_TESTS -eq 2 ]; then
    echo -e "${GREEN}✓✓✓ TESTES DE SINCRONIZAÇÃO PASSARAM!${NC}"
    exit 0
else
    echo -e "${RED}✗✗✗ ALGUNS TESTES FALHARAM!${NC}"
    exit 1
fi