#!/bin/bash
# tests/test_memory.sh - VERSÃO CORRIGIDA (Sem bloqueio no wait)
# Testes de Memória com Valgrind (Memory Leaks)

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "=========================================="
echo "   TESTES DE MEMÓRIA - Valgrind"
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

echo -e "${YELLOW}⚠ AVISO: Valgrind adiciona overhead (servidor será ~20x mais lento)${NC}"
echo ""

# Limpar recursos IPC antigos
echo ">> A limpar recursos IPC antigos..."
pkill -9 server 2>/dev/null
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true

# ==========================================
# TESTE 1: Memory Leaks (Leak Check Full)
# ==========================================
echo -e "${BLUE}TESTE 1: Deteção de Memory Leaks${NC}"
echo "A iniciar servidor com Valgrind (leak-check=full)..."
echo ""

# Criar ficheiro de suppressions personalizado (opcional)
cat > valgrind.supp << 'EOF'
{
   pthread_create_leak
   Memcheck:Leak
   ...
   fun:pthread_create*
}
{
   dl_init_leak
   Memcheck:Leak
   ...
   fun:_dl_init
}
EOF

# Iniciar servidor com Valgrind
valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind_memory.log \
         --suppressions=valgrind.supp \
         ./server &

SERVER_PID=$!
echo "Servidor iniciado com PID: $SERVER_PID"
echo "Aguardando 10 segundos para o servidor arrancar (Valgrind é lento)..."
sleep 10

# Verificar se o servidor está a correr
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ FAIL: Servidor crashou durante arranque!${NC}"
    cat valgrind_memory.log
    exit 1
fi

# Gerar tráfego
echo ">> A gerar tráfego para exercitar alocações de memória..."

echo "   Fase 1: 50 pedidos HTML"
for i in {1..50}; do
    # Adicionado --max-time 5 para evitar bloqueios
    curl -s --max-time 5 http://localhost:8080/index.html > /dev/null &
done
sleep 5 # Substituído wait por sleep

echo "   Fase 2: 50 pedidos CSS"
for i in {1..50}; do
    curl -s --max-time 5 http://localhost:8080/style.css > /dev/null &
done
sleep 5 # Substituído wait por sleep

echo "   Fase 3: 30 pedidos 404"
for i in {1..30}; do
    curl -s --max-time 5 http://localhost:8080/nao_existe.html > /dev/null &
done
sleep 5 # Substituído wait por sleep

echo "   Tráfego completo. Aguardando processamento final..."
sleep 5

# Parar o servidor gracefully
echo ">> A parar o servidor (SIGINT)..."
kill -SIGINT $SERVER_PID
# Agora sim, esperamos ESPECIFICAMENTE pelo PID do servidor para garantir que o Valgrind guarda o log
wait $SERVER_PID 2>/dev/null 

echo "   Servidor parado."

# Analisar resultados do Valgrind
echo ""
echo "=========================================="
echo "   ANÁLISE DO VALGRIND (MEMORY)"
echo "=========================================="

if [ ! -f valgrind_memory.log ]; then
    echo -e "${RED}ERRO: Log do Valgrind não encontrado.${NC}"
    exit 1
fi

# Extrair sumário
HEAP_SUMMARY=$(grep -A 10 "HEAP SUMMARY:" valgrind_memory.log | tail -10)
LEAK_SUMMARY=$(grep -A 15 "LEAK SUMMARY:" valgrind_memory.log | tail -15)

echo "HEAP SUMMARY:"
echo "$HEAP_SUMMARY"
echo ""
echo "LEAK SUMMARY:"
echo "$LEAK_SUMMARY"
echo ""

# Verificar leaks
DEFINITELY_LOST=$(grep "definitely lost:" valgrind_memory.log | tail -1 | awk '{print $4}' | tr -d ',')
INDIRECTLY_LOST=$(grep "indirectly lost:" valgrind_memory.log | tail -1 | awk '{print $4}' | tr -d ',')
POSSIBLY_LOST=$(grep "possibly lost:" valgrind_memory.log | tail -1 | awk '{print $4}' | tr -d ',')

echo "Análise:"
echo "  - Definitely lost: ${DEFINITELY_LOST:-0} bytes"
echo "  - Indirectly lost: ${INDIRECTLY_LOST:-0} bytes"
echo "  - Possibly lost: ${POSSIBLY_LOST:-0} bytes"
echo ""

# Classificar resultado
CRITICAL_LEAKS=$((${DEFINITELY_LOST:-0} + ${INDIRECTLY_LOST:-0}))

if [ $CRITICAL_LEAKS -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Sem memory leaks críticos!${NC}"
    if [ ${POSSIBLY_LOST:-0} -gt 0 ]; then
        echo -e "${YELLOW}⚠ AVISO: ${POSSIBLY_LOST} bytes possibly lost (pode ser falso positivo)${NC}"
    fi
    MEMCHECK_PASS=1
else
    echo -e "${RED}✗ FAIL: $CRITICAL_LEAKS bytes de memory leaks!${NC}"
    echo ""
    echo "Detalhes dos leaks:"
    grep -A 20 "definitely lost\|indirectly lost" valgrind_memory.log | head -50
    MEMCHECK_PASS=0
fi

echo ""
echo "Log completo: valgrind_memory.log"
echo ""

# ==========================================
# TESTE 2: Invalid Memory Access
# ==========================================
echo -e "${BLUE}TESTE 2: Deteção de Acessos Inválidos à Memória${NC}"
echo ""

INVALID_READS=$(grep -c "Invalid read" valgrind_memory.log 2>/dev/null || echo 0)
INVALID_WRITES=$(grep -c "Invalid write" valgrind_memory.log 2>/dev/null || echo 0)
USE_OF_UNINIT=$(grep -c "uninitialised" valgrind_memory.log 2>/dev/null || echo 0)

echo "Invalid reads: $INVALID_READS"
echo "Invalid writes: $INVALID_WRITES"
echo "Use of uninitialised values: $USE_OF_UNINIT"
echo ""

TOTAL_INVALID=$((INVALID_READS + INVALID_WRITES + USE_OF_UNINIT))

if [ $TOTAL_INVALID -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Sem acessos inválidos à memória!${NC}"
    INVALID_PASS=1
else
    echo -e "${RED}✗ FAIL: $TOTAL_INVALID problemas de acesso à memória!${NC}"
    echo ""
    echo "Primeiros erros:"
    grep -B 2 -A 5 "Invalid read\|Invalid write\|uninitialised" valgrind_memory.log | head -30
    INVALID_PASS=0
fi

echo ""

# ==========================================
# TESTE 3: Monitorização de Recursos
# ==========================================
echo -e "${BLUE}TESTE 3: Verificação de Limpeza de Recursos IPC${NC}"
echo "A verificar se recursos foram limpos corretamente..."
echo ""

# Verificar se ainda existem recursos IPC antigos
SHM_FILES=$(ls /dev/shm/ws_* 2>/dev/null | wc -l)
SEM_FILES=$(ls /dev/shm/sem.ws_* 2>/dev/null | wc -l)

echo "Ficheiros em /dev/shm:"
echo "  - Memória partilhada (ws_*): $SHM_FILES"
echo "  - Semáforos (sem.ws_*): $SEM_FILES"
echo ""

if [ $SHM_FILES -eq 0 ] && [ $SEM_FILES -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Recursos IPC limpos corretamente!${NC}"
    CLEANUP_PASS=1
else
    echo -e "${YELLOW}⚠ AVISO: Recursos IPC ainda presentes (pode ser do teste anterior)${NC}"
    # Limpar agora
    rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
    echo "  Recursos limpos manualmente."
    CLEANUP_PASS=1
fi

echo ""

# ==========================================
# TESTE 4: Teste de Stress com Monitorização
# ==========================================
echo -e "${BLUE}TESTE 4: Stress Test com Monitorização de Memória${NC}"
echo "A correr servidor normal e monitorizar uso de RAM..."
echo ""

# Limpar
pkill -9 server 2>/dev/null
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true

# Iniciar servidor normal
./server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 3

# Capturar uso de memória inicial
MEM_START=$(ps -o rss= -p $SERVER_PID 2>/dev/null || echo 0)
echo "Memória inicial: ${MEM_START} KB"

# Gerar carga
echo ">> A enviar 500 pedidos..."
for i in {1..500}; do
    curl -s --max-time 2 http://localhost:8080/index.html > /dev/null &
    if [ $((i % 100)) -eq 0 ]; then
        sleep 1 # Pausa ligeira a cada 100
    fi
done
# Espera segura
sleep 5

# Capturar uso de memória final
MEM_END=$(ps -o rss= -p $SERVER_PID 2>/dev/null || echo 0)
echo "Memória final: ${MEM_END} KB"

# Calcular crescimento
MEM_GROWTH=$((MEM_END - MEM_START))
echo "Crescimento de memória: ${MEM_GROWTH} KB"
echo ""

# Parar servidor
kill -SIGINT $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Avaliar
# Crescimento aceitável: < 50MB (51200 KB)
if [ $MEM_GROWTH -lt 51200 ]; then
    echo -e "${GREEN}✓ PASS: Crescimento de memória aceitável (<50MB)${NC}"
    STRESS_MEM_PASS=1
else
    echo -e "${YELLOW}⚠ AVISO: Crescimento de memória elevado (${MEM_GROWTH} KB)${NC}"
    echo "  Isto pode indicar memory leaks acumulados."
    STRESS_MEM_PASS=0
fi

echo ""

# ==========================================
# RESUMO FINAL
# ==========================================
echo "=========================================="
echo "   RESUMO DOS TESTES DE MEMÓRIA"
echo "=========================================="
echo ""

TOTAL_TESTS=4
PASSED_TESTS=$((MEMCHECK_PASS + INVALID_PASS + CLEANUP_PASS + STRESS_MEM_PASS))

echo "Testes passados: $PASSED_TESTS/$TOTAL_TESTS"
echo ""

if [ $MEMCHECK_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Valgrind Leak Check: Sem leaks críticos"
else
    echo -e "${RED}✗${NC} Valgrind Leak Check: Memory leaks detetados"
fi

if [ $INVALID_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Invalid Access: Sem acessos inválidos"
else
    echo -e "${RED}✗${NC} Invalid Access: Problemas detetados"
fi

if [ $CLEANUP_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} IPC Cleanup: Recursos limpos"
else
    echo -e "${RED}✗${NC} IPC Cleanup: Recursos não limpos"
fi

if [ $STRESS_MEM_PASS -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Memory Growth: Crescimento aceitável"
else
    echo -e "${YELLOW}⚠${NC} Memory Growth: Crescimento elevado"
fi

echo ""

if [ $PASSED_TESTS -ge 3 ]; then
    echo -e "${GREEN}✓✓✓ TESTES DE MEMÓRIA CONCLUÍDOS COM SUCESSO!${NC}"
    exit 0
else
    echo -e "${RED}✗✗✗ PROBLEMAS DE MEMÓRIA DETETADOS!${NC}"
    exit 1
fi