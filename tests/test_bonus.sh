#!/bin/bash
# tests/test_bonus.sh
# Teste Rápido dos Bónus: Dashboard, VHosts e Keep-Alive
# Necessário correr "./server" num terminal antes de executar este script.

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'
SERVER_URL="http://localhost:8080"

echo "=========================================="
echo "   TESTE DE BÓNUS (OPCIONAIS)"
echo "=========================================="

# 0. Preparação (Garantir que os sites existem)
mkdir -p www/site1 www/site2
echo "<h1>Site 1 - Bonus</h1>" > www/site1/index.html
echo "<h1>Site 2 - Bonus</h1>" > www/site2/index.html

# ---------------------------------------------------------
# TESTE 1: Dashboard (/stats)
# ---------------------------------------------------------
echo -n "1. Testing Dashboard (/stats)... "
HTTP_CODE=$(curl -s -o /tmp/stats_output.html -w "%{http_code}" "$SERVER_URL/stats")
if [ "$HTTP_CODE" -eq 200 ] && grep -q "Server Dashboard" /tmp/stats_output.html; then
    echo -e "${GREEN}[ PASS ]${NC}"
else
    echo -e "${RED}[ FAIL ]${NC} (Code: $HTTP_CODE ou conteúdo incorreto)"
fi

# ---------------------------------------------------------
# TESTE 2: Virtual Hosts
# ---------------------------------------------------------
echo -n "2. Testing Virtual Host (site1.local)... "
CONTENT=$(curl -s -H "Host: site1.local" "$SERVER_URL/index.html")
if [[ "$CONTENT" == *"Site 1 - Bonus"* ]]; then
    echo -e "${GREEN}[ PASS ]${NC}"
else
    echo -e "${RED}[ FAIL ]${NC} (Recebido: $CONTENT)"
fi

echo -n "3. Testing Virtual Host (site2.local)... "
CONTENT=$(curl -s -H "Host: site2.local" "$SERVER_URL/index.html")
if [[ "$CONTENT" == *"Site 2 - Bonus"* ]]; then
    echo -e "${GREEN}[ PASS ]${NC}"
else
    echo -e "${RED}[ FAIL ]${NC} (Recebido: $CONTENT)"
fi

# ---------------------------------------------------------
# TESTE 3: Keep-Alive
# ---------------------------------------------------------
echo -n "4. Testing Keep-Alive Header... "
# Verifica se o header Connection: keep-alive está presente na resposta
HEADER=$(curl -s -I "$SERVER_URL/index.html" | grep -i "Connection: keep-alive")
if [ -n "$HEADER" ]; then
    echo -e "${GREEN}[ PASS ]${NC}"
else
    echo -e "${RED}[ FAIL ]${NC} (Header não encontrado ou é 'close')"
fi

echo ""
echo "Teste concluído."
rm -f /tmp/stats_output.html