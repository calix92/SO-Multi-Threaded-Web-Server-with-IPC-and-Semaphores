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

if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}ERRO: Valgrind não está instalado!${NC}"
    exit 1
fi

# 1. Preparar Suppressions
cat > valgrind_helgrind.supp << 'EOF'
{
   glibc_timezone
   Helgrind:Race
   ...
   fun:__tz*
}
{
   glibc_localtime
   Helgrind:Race
   ...
   fun:localtime_r
}
{
   pthread_create_internal
   Helgrind:Race
   ...
   fun:pthread_create*
}
{
   dl_init
   Helgrind:Race
   ...
   fun:_dl_init
}
{
   clone_race
   Helgrind:Race
   ...
   fun:clone
}
EOF

# Limpar memória
rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
pkill -9 server 2>/dev/null

echo -e "${BLUE}>> A iniciar servidor com Helgrind...${NC}"

# Iniciar Helgrind
valgrind --tool=helgrind \
         --suppressions=valgrind_helgrind.supp \
         --log-file=helgrind_output.log \
         ./server &

SERVER_PID=$!
echo "PID do Servidor: $SERVER_PID"

echo "A aguardar 10 segundos para o servidor estabilizar..."
sleep 10

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}ERRO: O servidor morreu no arranque.${NC}"
    cat helgrind_output.log
    exit 1
fi

echo -e "${BLUE}>> A enviar tráfego CONTROLADO...${NC}"

# Enviar 20 pedidos com timeout forçado
for i in {1..20}; do
    curl -s --max-time 2 "http://localhost:8080/index.html" > /dev/null &
    # Pausa ligeira para não afogar o Helgrind
    if [ $((i % 5)) -eq 0 ]; then sleep 1; fi
done

echo "A aguardar que os pedidos terminem..."
sleep 5 

echo -e "${BLUE}>> A terminar o servidor...${NC}"
kill -SIGINT $SERVER_PID
# Esperamos especificamente pelo servidor agora
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "   ANÁLISE DE RESULTADOS"
echo "=========================================="

if [ ! -f helgrind_output.log ]; then
    echo -e "${RED}ERRO: Log não gerado.${NC}"
    exit 1
fi

# Contar erros
ERRORS=$(grep "Possible data race" helgrind_output.log | wc -l)
LOCK_ERRORS=$(grep "lock order violation" helgrind_output.log | wc -l)

echo "Race Conditions detetadas: $ERRORS"
echo "Erros de Lock Order: $LOCK_ERRORS"
echo ""

if [ $ERRORS -eq 0 ] && [ $LOCK_ERRORS -eq 0 ]; then
    echo -e "${GREEN}[ OK ] PASS: Nenhuma race condition detetada!${NC}"
    if grep -q "failed with error code 4" helgrind_output.log; then
        echo -e "${YELLOW}(Nota: Erro 'EINTR' ignorado - normal no shutdown)${NC}"
    fi
else
    echo -e "${RED}[ERROR] FAIL: Problemas de sincronização encontrados.${NC}"
    echo "Detalhes (top 10):"
    grep -A 5 "Possible data race" helgrind_output.log | head -20
fi

# Limpeza
rm -f valgrind_helgrind.supp