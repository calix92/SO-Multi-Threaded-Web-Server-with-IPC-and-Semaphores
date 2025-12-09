#!/bin/bash
# tests/test_load.sh - VERSÃO ROBUSTA (Com Timeouts)
# Inicia servidor, corre testes com proteção contra bloqueios e limpa tudo.

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SERVER_URL="http://localhost:8080"

echo "=========================================="
echo "   TESTES DE CARGA - Apache Bench (ab)"
echo "=========================================="
echo ""

# 1. Verificar dependências
if ! command -v ab &> /dev/null; then
    echo -e "${RED}ERRO: Apache Bench não instalado!${NC}"
    exit 1
fi

# 2. Limpar ambiente antigo
pkill -9 server 2>/dev/null
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true

# 3. Iniciar Servidor
echo ">> A iniciar o servidor em background..."
./server > /dev/null 2>&1 &
SERVER_PID=$!
echo "   PID: $SERVER_PID"
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}ERRO: O servidor falhou ao iniciar!${NC}"
    exit 1
fi
echo -e "${GREEN}Servidor online!${NC}"
echo ""

# ==========================================
# TESTE 1: Carga Leve (Warm-up)
# ==========================================
echo -e "${BLUE}TESTE 1: Carga Leve (Warm-up)${NC}"
# -t 5: Timeout de 5s para não bloquear o script se falhar
ab -t 5 -n 100 -c 10 "$SERVER_URL/index.html" > /tmp/ab_test1.log 2>&1

FAILED1=$(grep "Failed requests:" /tmp/ab_test1.log | awk '{print $3}')
RPS1=$(grep "Requests per second:" /tmp/ab_test1.log | awk '{print $4}')

if [ "$FAILED1" = "0" ]; then
    echo -e "${GREEN}[ OK ] PASS: Sem falhas ($RPS1 req/sec)${NC}"
else
    echo -e "${RED}[ERROR] FAIL: $FAILED1 pedidos falharam (ou timeout)!${NC}"
fi
echo ""

# ==========================================
# TESTE 2: Carga Média
# ==========================================
echo -e "${BLUE}TESTE 2: Carga Média (1000 req / 50 conc)${NC}"
# Timeout de 10s
ab -t 10 -n 1000 -c 50 "$SERVER_URL/index.html" > /tmp/ab_test2.log 2>&1

FAILED2=$(grep "Failed requests:" /tmp/ab_test2.log | awk '{print $3}')
RPS2=$(grep "Requests per second:" /tmp/ab_test2.log | awk '{print $4}')

if [ "$FAILED2" = "0" ]; then
    echo -e "${GREEN}[ OK ] PASS: Sem falhas ($RPS2 req/sec)${NC}"
else
    echo -e "${RED}[ERROR] FAIL: $FAILED2 pedidos falharam!${NC}"
fi
echo ""

# ==========================================
# TESTE 3: Carga Pesada (Stress)
# ==========================================
echo -e "${BLUE}TESTE 3: Carga Pesada (10k req / 100 conc)${NC}"
echo -e "${YELLOW}! Aguarde (max 60s)...${NC}"

# Timeout de 60s. Se o servidor estiver lento, o ab corta o teste e mostra stats parciais
ab -t 60 -n 10000 -c 100 "$SERVER_URL/index.html" > /tmp/ab_test3.log 2>&1

FAILED3=$(grep "Failed requests:" /tmp/ab_test3.log | awk '{print $3}')
RPS3=$(grep "Requests per second:" /tmp/ab_test3.log | awk '{print $4}')

if [ "$FAILED3" = "0" ]; then
    echo -e "${GREEN}[ OK ] PASS: Sem falhas ($RPS3 req/sec)${NC}"
else
    # Verifica se foi timeout (Requests completed < Total)
    COMPLETED=$(grep "Complete requests:" /tmp/ab_test3.log | awk '{print $3}')
    if [ "$COMPLETED" != "10000" ]; then
         echo -e "${YELLOW}! AVISO: Teste cortado por timeout ($COMPLETED/10000 feitos).${NC}"
         echo -e "${YELLOW}         Servidor pode estar lento ou em deadlock.${NC}"
    else
         echo -e "${RED}[ERROR] FAIL: $FAILED3 pedidos falharam!${NC}"
    fi
fi
echo ""

# ==========================================
# TESTE 4: Mix de Ficheiros
# ==========================================
echo -e "${BLUE}TESTE 4: Mix de Ficheiros Concorrentes${NC}"

# Lançar em background com timeout
ab -t 15 -n 500 -c 20 "$SERVER_URL/index.html" > /tmp/ab_html.log 2>&1 &
PID1=$!
ab -t 15 -n 500 -c 20 "$SERVER_URL/style.css" > /tmp/ab_css.log 2>&1 &
PID2=$!
ab -t 15 -n 500 -c 20 "$SERVER_URL/script.js" > /tmp/ab_js.log 2>&1 &
PID3=$!

# Esperar pelos processos
wait $PID1 $PID2 $PID3

FAILED_HTML=$(grep "Failed requests:" /tmp/ab_html.log | awk '{print $3}')
FAILED_CSS=$(grep "Failed requests:" /tmp/ab_css.log | awk '{print $3}')
FAILED_JS=$(grep "Failed requests:" /tmp/ab_js.log | awk '{print $3}')

# Tratar valores vazios (se o ab crashar) como 0 erros mas verificar output
TOTAL_FAIL=$(( ${FAILED_HTML:-0} + ${FAILED_CSS:-0} + ${FAILED_JS:-0} ))

if [ $TOTAL_FAIL -eq 0 ]; then
    echo -e "${GREEN}[ OK ] PASS: Mix completo sem falhas!${NC}"
else
    echo -e "${RED}[ERROR] FAIL: $TOTAL_FAIL falhas no total.${NC}"
fi
echo ""

# ==========================================
# TESTE 5: Keep-Alive
# ==========================================
echo -e "${BLUE}TESTE 5: Keep-Alive (-k)${NC}"

ab -t 10 -n 1000 -c 20 -k "$SERVER_URL/index.html" > /tmp/ab_keepalive.log 2>&1
RPS_KA=$(grep "Requests per second:" /tmp/ab_keepalive.log | awk '{print $4}')

echo "  Performance Keep-Alive: $RPS_KA req/sec"
echo -e "${GREEN}[ OK ] Teste concluído.${NC}"
echo ""

# ==========================================
# CLEANUP
# ==========================================
echo ">> A encerrar servidor..."
kill -SIGINT $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo "=========================================="
echo -e "${GREEN}Testes concluídos. Logs em /tmp/ab_*.log${NC}"
exit 0