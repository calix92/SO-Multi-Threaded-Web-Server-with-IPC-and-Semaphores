#!/bin/bash
# tests/run_all_tests.sh
# Suite Mestre de Testes - Versão Compatível com Scripts Autónomos

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo "╔════════════════════════════════════════════════════╗"
echo "║     SUITE COMPLETA DE TESTES - ConcurrentHTTP     ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

# Verificar diretório
if [ ! -f "Makefile" ]; then
    echo -e "${RED}ERRO: Execute na raiz do projeto!${NC}"
    exit 1
fi

# Preparar resultados
mkdir -p test_results
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="test_results/$TIMESTAMP"
mkdir -p "$RESULTS_DIR"

echo -e "${CYAN}Resultados em: $RESULTS_DIR${NC}"
echo ""

# ==========================================
# FASE 1: Compilação
# ==========================================
echo -e "${BLUE}▶ FASE 1: COMPILAÇÃO${NC}"
make clean > /dev/null 2>&1
if make > "$RESULTS_DIR/compilation.log" 2>&1; then
    echo -e "${GREEN}✓ Sucesso${NC}"
    COMPILE_PASS=1
else
    echo -e "${RED}✗ Erro${NC}"
    cat "$RESULTS_DIR/compilation.log"
    exit 1
fi
echo ""

# ==========================================
# FASE 2: Testes Funcionais
# ==========================================
# Este teste NÃO é autónomo, precisamos de ligar o servidor aqui
echo -e "${BLUE}▶ FASE 2: TESTES FUNCIONAIS${NC}"

# Limpar e Iniciar
pkill -9 server 2>/dev/null
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
./server > "$RESULTS_DIR/server_functional.log" 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ Servidor não arrancou!${NC}"
    FUNCTIONAL_PASS=0
else
    if bash tests/test_functional.sh > "$RESULTS_DIR/functional_tests.log" 2>&1; then
        echo -e "${GREEN}✓ Sucesso${NC}"
        FUNCTIONAL_PASS=1
    else
        echo -e "${RED}✗ Falhas detetadas${NC}"
        FUNCTIONAL_PASS=0
    fi
    # Parar servidor
    kill -SIGINT $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
fi
echo ""

# ==========================================
# FASE 3: Testes de Carga
# ==========================================
echo -e "${BLUE}▶ FASE 3: TESTES DE CARGA${NC}"
# O script test_load.sh agora é autónomo, chamamos direto
if bash tests/test_load.sh > "$RESULTS_DIR/load_tests.log" 2>&1; then
    echo -e "${GREEN}✓ Sucesso${NC}"
    LOAD_PASS=1
else
    echo -e "${RED}✗ Falhas detetadas${NC}"
    LOAD_PASS=0
fi
echo ""

# ==========================================
# FASE 4: Testes de Sincronização
# ==========================================
echo -e "${BLUE}▶ FASE 4: TESTES DE SINCRONIZAÇÃO${NC}"
# Removemos a pergunta interativa para correr direto
echo "A executar Helgrind (pode demorar)..."
if bash tests/test_sync.sh > "$RESULTS_DIR/sync_tests.log" 2>&1; then
    echo -e "${GREEN}✓ Sucesso${NC}"
    SYNC_PASS=1
else
    echo -e "${RED}✗ Falhas detetadas${NC}"
    SYNC_PASS=0
fi
cp helgrind_output.log "$RESULTS_DIR/" 2>/dev/null || true
echo ""

# ==========================================
# FASE 5: Testes de Memória
# ==========================================
echo -e "${BLUE}▶ FASE 5: TESTES DE MEMÓRIA${NC}"
echo "A executar Valgrind (pode demorar)..."
if bash tests/test_memory.sh > "$RESULTS_DIR/memory_tests.log" 2>&1; then
    echo -e "${GREEN}✓ Sucesso${NC}"
    MEMORY_PASS=1
else
    echo -e "${RED}✗ Falhas detetadas${NC}"
    MEMORY_PASS=0
fi
cp valgrind_memory.log "$RESULTS_DIR/" 2>/dev/null || true
echo ""

# ==========================================
# RELATÓRIO FINAL
# ==========================================
echo "╔════════════════════════════════════╗"
echo "║          RESUMO FINAL              ║"
echo "╚════════════════════════════════════╝"
printf "%-25s %s\n" "Compilação:" "$([ $COMPILE_PASS -eq 1 ] && echo -e "${GREEN}PASS${NC}" || echo -e "${RED}FAIL${NC}")"
printf "%-25s %s\n" "Funcionais:" "$([ $FUNCTIONAL_PASS -eq 1 ] && echo -e "${GREEN}PASS${NC}" || echo -e "${RED}FAIL${NC}")"
printf "%-25s %s\n" "Carga:" "$([ $LOAD_PASS -eq 1 ] && echo -e "${GREEN}PASS${NC}" || echo -e "${RED}FAIL${NC}")"
printf "%-25s %s\n" "Sincronização:" "$([ $SYNC_PASS -eq 1 ] && echo -e "${GREEN}PASS${NC}" || echo -e "${RED}FAIL${NC}")"
printf "%-25s %s\n" "Memória:" "$([ $MEMORY_PASS -eq 1 ] && echo -e "${GREEN}PASS${NC}" || echo -e "${RED}FAIL${NC}")"
echo ""

# Limpeza final
pkill -9 server 2>/dev/null
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true