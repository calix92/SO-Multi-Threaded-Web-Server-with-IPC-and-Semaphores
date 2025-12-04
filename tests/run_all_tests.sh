#!/bin/bash
# tests/run_all_tests.sh
# Script Mestre para Executar Todos os Testes

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo "╔════════════════════════════════════════════════════╗"
echo "║     SUITE COMPLETA DE TESTES - ConcurrentHTTP     ║"
echo "║        Sistemas Operativos - UA 2025/2026         ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

# Verificar se estamos no diretório correto
if [ ! -f "Makefile" ]; then
    echo -e "${RED}ERRO: Execute este script a partir do diretório raiz do projeto!${NC}"
    exit 1
fi

# Criar diretório de resultados
mkdir -p test_results
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="test_results/$TIMESTAMP"
mkdir -p "$RESULTS_DIR"

echo -e "${CYAN}Diretório de resultados: $RESULTS_DIR${NC}"
echo ""

# ==========================================
# FASE 1: Compilação
# ==========================================
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}FASE 1: COMPILAÇÃO DO PROJETO${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

make clean > /dev/null 2>&1
echo ">> A compilar..."
if make > "$RESULTS_DIR/compilation.log" 2>&1; then
    echo -e "${GREEN}✓ Compilação bem-sucedida!${NC}"
    COMPILE_PASS=1
else
    echo -e "${RED}✗ Erro na compilação!${NC}"
    echo "Veja os detalhes em: $RESULTS_DIR/compilation.log"
    cat "$RESULTS_DIR/compilation.log"
    exit 1
fi
echo ""

# ==========================================
# FASE 2: Testes Funcionais
# ==========================================
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}FASE 2: TESTES FUNCIONAIS${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

# Iniciar servidor em background
echo ">> A iniciar servidor..."
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
./server > "$RESULTS_DIR/server_functional.log" 2>&1 &
SERVER_PID=$!
echo "   Servidor iniciado (PID: $SERVER_PID)"
sleep 3

# Verificar se iniciou corretamente
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ Servidor não arrancou! Veja: $RESULTS_DIR/server_functional.log${NC}"
    cat "$RESULTS_DIR/server_functional.log"
    exit 1
fi

# Executar testes funcionais
echo ">> A executar testes funcionais..."
if bash tests/test_functional.sh > "$RESULTS_DIR/functional_tests.log" 2>&1; then
    echo -e "${GREEN}✓ Testes funcionais passaram!${NC}"
    FUNCTIONAL_PASS=1
else
    echo -e "${RED}✗ Alguns testes funcionais falharam!${NC}"
    FUNCTIONAL_PASS=0
fi

# Mostrar resumo
grep -E "PASS|FAIL" "$RESULTS_DIR/functional_tests.log" | head -15

# Parar servidor
kill -SIGINT $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
sleep 2
echo ""

# ==========================================
# FASE 3: Testes de Carga
# ==========================================
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}FASE 3: TESTES DE CARGA (Apache Bench)${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

# Verificar se ab está instalado
if ! command -v ab &> /dev/null; then
    echo -e "${YELLOW}⚠ Apache Bench não instalado. A saltar testes de carga.${NC}"
    echo "   Instale com: sudo apt-get install apache2-utils"
    LOAD_PASS=0
else
    # Iniciar servidor
    echo ">> A iniciar servidor..."
    rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
    ./server > "$RESULTS_DIR/server_load.log" 2>&1 &
    SERVER_PID=$!
    sleep 3

    # Executar testes de carga
    echo ">> A executar testes de carga (pode demorar 2-3 minutos)..."
    if bash tests/test_load.sh > "$RESULTS_DIR/load_tests.log" 2>&1; then
        echo -e "${GREEN}✓ Testes de carga passaram!${NC}"
        LOAD_PASS=1
    else
        echo -e "${RED}✗ Testes de carga falharam!${NC}"
        LOAD_PASS=0
    fi

    # Mostrar métricas principais
    echo ""
    echo "Métricas de Performance:"
    grep "req/sec" "$RESULTS_DIR/load_tests.log" | head -5

    # Parar servidor
    kill -SIGINT $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    sleep 2
fi
echo ""

# ==========================================
# FASE 4: Testes de Sincronização
# ==========================================
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}FASE 4: TESTES DE SINCRONIZAÇÃO${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

# Verificar se valgrind está instalado
if ! command -v valgrind &> /dev/null; then
    echo -e "${YELLOW}⚠ Valgrind não instalado. A saltar testes de sincronização.${NC}"
    echo "   Instale com: sudo apt-get install valgrind"
    SYNC_PASS=0
else
    echo -e "${YELLOW}⚠ AVISO: Testes com Helgrind são MUITO LENTOS (5-10 min)${NC}"
    echo "Deseja executar? (s/N)"
    read -t 10 -n 1 RESPONSE
    echo ""
    
    if [[ $RESPONSE =~ ^[Ss]$ ]]; then
        if bash tests/test_sync.sh > "$RESULTS_DIR/sync_tests.log" 2>&1; then
            echo -e "${GREEN}✓ Testes de sincronização passaram!${NC}"
            SYNC_PASS=1
        else
            echo -e "${RED}✗ Problemas de sincronização detetados!${NC}"
            SYNC_PASS=0
        fi
        
        # Copiar logs do Valgrind
        cp helgrind_output.log "$RESULTS_DIR/" 2>/dev/null || true
    else
        echo -e "${YELLOW}⊘ Testes de sincronização saltados pelo utilizador${NC}"
        SYNC_PASS=0
    fi
fi
echo ""

# ==========================================
# FASE 5: Testes de Memória
# ==========================================
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}FASE 5: TESTES DE MEMÓRIA (Valgrind)${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

if ! command -v valgrind &> /dev/null; then
    echo -e "${YELLOW}⚠ Valgrind não instalado. A saltar testes de memória.${NC}"
    MEMORY_PASS=0
else
    echo ">> A executar testes de memória..."
    if bash tests/test_memory.sh > "$RESULTS_DIR/memory_tests.log" 2>&1; then
        echo -e "${GREEN}✓ Sem memory leaks críticos!${NC}"
        MEMORY_PASS=1
    else
        echo -e "${RED}✗ Memory leaks detetados!${NC}"
        MEMORY_PASS=0
    fi
    
    # Copiar logs do Valgrind
    cp valgrind_memory.log "$RESULTS_DIR/" 2>/dev/null || true
    
    # Mostrar resumo de leaks
    echo ""
    echo "Sumário de Memória:"
    grep -A 5 "LEAK SUMMARY" "$RESULTS_DIR/valgrind_memory.log" 2>/dev/null | head -10
fi
echo ""

# ==========================================
# FASE 6: Limpeza Final
# ==========================================
echo ">> A limpar recursos..."
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
pkill -9 server 2>/dev/null || true
echo ""

# ==========================================
# RELATÓRIO FINAL
# ==========================================
echo ""
echo "╔════════════════════════════════════════════════════╗"
echo "║              RELATÓRIO FINAL DE TESTES             ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

TOTAL_PHASES=5
PASSED_PHASES=$((COMPILE_PASS + FUNCTIONAL_PASS + LOAD_PASS + SYNC_PASS + MEMORY_PASS))

# Tabela de resultados
printf "%-30s %s\n" "FASE" "RESULTADO"
printf "%-30s %s\n" "────────────────────────────" "──────────"

if [ $COMPILE_PASS -eq 1 ]; then
    printf "%-30s ${GREEN}✓ PASS${NC}\n" "1. Compilação"
else
    printf "%-30s ${RED}✗ FAIL${NC}\n" "1. Compilação"
fi

if [ $FUNCTIONAL_PASS -eq 1 ]; then
    printf "%-30s ${GREEN}✓ PASS${NC}\n" "2. Testes Funcionais"
else
    printf "%-30s ${RED}✗ FAIL${NC}\n" "2. Testes Funcionais"
fi

if [ $LOAD_PASS -eq 1 ]; then
    printf "%-30s ${GREEN}✓ PASS${NC}\n" "3. Testes de Carga"
elif [ $LOAD_PASS -eq 0 ] && command -v ab &> /dev/null; then
    printf "%-30s ${RED}✗ FAIL${NC}\n" "3. Testes de Carga"
else
    printf "%-30s ${YELLOW}⊘ SKIP${NC}\n" "3. Testes de Carga"
fi

if [ $SYNC_PASS -eq 1 ]; then
    printf "%-30s ${GREEN}✓ PASS${NC}\n" "4. Testes de Sincronização"
elif [ $SYNC_PASS -eq 0 ] && command -v valgrind &> /dev/null; then
    printf "%-30s ${RED}✗ FAIL${NC}\n" "4. Testes de Sincronização"
else
    printf "%-30s ${YELLOW}⊘ SKIP${NC}\n" "4. Testes de Sincronização"
fi

if [ $MEMORY_PASS -eq 1 ]; then
    printf "%-30s ${GREEN}✓ PASS${NC}\n" "5. Testes de Memória"
elif [ $MEMORY_PASS -eq 0 ] && command -v valgrind &> /dev/null; then
    printf "%-30s ${RED}✗ FAIL${NC}\n" "5. Testes de Memória"
else
    printf "%-30s ${YELLOW}⊘ SKIP${NC}\n" "5. Testes de Memória"
fi

echo ""
echo "╔════════════════════════════════════════════════════╗"

if [ $PASSED_PHASES -eq $TOTAL_PHASES ]; then
    echo -e "║  ${GREEN}✓✓✓ TODOS OS TESTES PASSARAM! PROJETO VALIDADO! ✓✓✓${NC}  ║"
elif [ $PASSED_PHASES -ge 3 ]; then
    echo -e "║  ${YELLOW}⚠ Maioria dos testes passou. Verifique as falhas.${NC}  ║"
else
    echo -e "║  ${RED}✗✗✗ VÁRIOS TESTES FALHARAM! CORREÇÕES NECESSÁRIAS${NC}  ║"
fi

echo "╚════════════════════════════════════════════════════╝"
echo ""

echo "Todos os logs foram guardados em: $RESULTS_DIR"
echo ""
echo "Ficheiros gerados:"
ls -lh "$RESULTS_DIR"
echo ""

# Criar relatório em texto
cat > "$RESULTS_DIR/REPORT.txt" << EOF
═══════════════════════════════════════════════════════════
    RELATÓRIO DE TESTES - ConcurrentHTTP Server
    Data: $(date)
═══════════════════════════════════════════════════════════

RESULTADOS:
-----------
Compilação:           $([ $COMPILE_PASS -eq 1 ] && echo "PASS" || echo "FAIL")
Testes Funcionais:    $([ $FUNCTIONAL_PASS -eq 1 ] && echo "PASS" || echo "FAIL")
Testes de Carga:      $([ $LOAD_PASS -eq 1 ] && echo "PASS" || echo "SKIP/FAIL")
Sincronização:        $([ $SYNC_PASS -eq 1 ] && echo "PASS" || echo "SKIP/FAIL")
Memória:              $([ $MEMORY_PASS -eq 1 ] && echo "PASS" || echo "SKIP/FAIL")

TOTAL: $PASSED_PHASES/$TOTAL_PHASES fases bem-sucedidas

FICHEIROS DE LOG:
-----------------
- compilation.log
- functional_tests.log
- load_tests.log
- sync_tests.log
- memory_tests.log
- valgrind_memory.log (se executado)
- helgrind_output.log (se executado)

═══════════════════════════════════════════════════════════
EOF

echo -e "${CYAN}Relatório completo guardado em: $RESULTS_DIR/REPORT.txt${NC}"
echo ""

if [ $PASSED_PHASES -ge 3 ]; then
    exit 0
else
    exit 1
fi