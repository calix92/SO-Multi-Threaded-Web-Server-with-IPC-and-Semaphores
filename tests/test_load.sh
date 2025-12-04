#!/bin/bash
# tests/test_load.sh - VERSÃO COMPLETA
# Testes de Carga com Apache Bench

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

# Verificar se o ab está instalado
if ! command -v ab &> /dev/null; then
    echo -e "${RED}ERRO: Apache Bench (ab) não está instalado!${NC}"
    echo "Instale com: sudo apt-get install apache2-utils"
    exit 1
fi

# Verificar se o servidor está a correr
echo ">> A verificar se o servidor está online..."
if ! curl -s --head "$SERVER_URL" > /dev/null; then
    echo -e "${RED}ERRO: Servidor não está a correr em $SERVER_URL${NC}"
    echo "Execute 'make run' noutra terminal primeiro!"
    exit 1
fi
echo -e "${GREEN}Servidor está online!${NC}"
echo ""

# ==========================================
# TESTE 1: Carga Leve (Warm-up)
# ==========================================
echo -e "${BLUE}TESTE 1: Carga Leve (Warm-up)${NC}"
echo "Configuração: 100 pedidos, 10 concorrentes"
echo "Comando: ab -n 100 -c 10 $SERVER_URL/index.html"
echo ""

ab -n 100 -c 10 "$SERVER_URL/index.html" > /tmp/ab_test1.log 2>&1

FAILED1=$(grep "Failed requests:" /tmp/ab_test1.log | awk '{print $3}')
COMPLETED1=$(grep "Complete requests:" /tmp/ab_test1.log | awk '{print $3}')
RPS1=$(grep "Requests per second:" /tmp/ab_test1.log | awk '{print $4}')

echo "Resultados:"
echo "  - Pedidos completados: $COMPLETED1"
echo "  - Pedidos falhados: $FAILED1"
echo "  - Requests/sec: $RPS1"

if [ "$FAILED1" = "0" ]; then
    echo -e "${GREEN}✓ PASS: Sem falhas!${NC}"
else
    echo -e "${RED}✗ FAIL: $FAILED1 pedidos falharam!${NC}"
fi
echo ""

# ==========================================
# TESTE 2: Carga Média
# ==========================================
echo -e "${BLUE}TESTE 2: Carga Média${NC}"
echo "Configuração: 1000 pedidos, 50 concorrentes"
echo "Comando: ab -n 1000 -c 50 $SERVER_URL/index.html"
echo ""

ab -n 1000 -c 50 "$SERVER_URL/index.html" > /tmp/ab_test2.log 2>&1

FAILED2=$(grep "Failed requests:" /tmp/ab_test2.log | awk '{print $3}')
COMPLETED2=$(grep "Complete requests:" /tmp/ab_test2.log | awk '{print $3}')
RPS2=$(grep "Requests per second:" /tmp/ab_test2.log | awk '{print $4}')
TIME2=$(grep "Time per request:" /tmp/ab_test2.log | head -1 | awk '{print $4}')

echo "Resultados:"
echo "  - Pedidos completados: $COMPLETED2"
echo "  - Pedidos falhados: $FAILED2"
echo "  - Requests/sec: $RPS2"
echo "  - Tempo/pedido (média): ${TIME2}ms"

if [ "$FAILED2" = "0" ]; then
    echo -e "${GREEN}✓ PASS: Sem falhas!${NC}"
else
    echo -e "${RED}✗ FAIL: $FAILED2 pedidos falharam!${NC}"
fi
echo ""

# ==========================================
# TESTE 3: Carga Pesada (Stress Test)
# ==========================================
echo -e "${BLUE}TESTE 3: Carga Pesada (Stress Test)${NC}"
echo "Configuração: 10000 pedidos, 100 concorrentes"
echo "Comando: ab -n 10000 -c 100 $SERVER_URL/index.html"
echo -e "${YELLOW}⚠ Este teste pode demorar 30-60 segundos...${NC}"
echo ""

ab -n 10000 -c 100 "$SERVER_URL/index.html" > /tmp/ab_test3.log 2>&1

FAILED3=$(grep "Failed requests:" /tmp/ab_test3.log | awk '{print $3}')
COMPLETED3=$(grep "Complete requests:" /tmp/ab_test3.log | awk '{print $3}')
RPS3=$(grep "Requests per second:" /tmp/ab_test3.log | awk '{print $4}')
TIME3=$(grep "Time per request:" /tmp/ab_test3.log | head -1 | awk '{print $4}')
TRANSFER3=$(grep "Transfer rate:" /tmp/ab_test3.log | awk '{print $3}')

echo "Resultados:"
echo "  - Pedidos completados: $COMPLETED3"
echo "  - Pedidos falhados: $FAILED3"
echo "  - Requests/sec: $RPS3"
echo "  - Tempo/pedido (média): ${TIME3}ms"
echo "  - Taxa de transferência: ${TRANSFER3} Kbytes/sec"

if [ "$FAILED3" = "0" ]; then
    echo -e "${GREEN}✓ PASS: Sem falhas sob carga pesada!${NC}"
else
    echo -e "${RED}✗ FAIL: $FAILED3 pedidos falharam sob stress!${NC}"
fi
echo ""

# ==========================================
# TESTE 4: Ficheiros Diferentes (Mix)
# ==========================================
echo -e "${BLUE}TESTE 4: Mix de Ficheiros (HTML, CSS, JS)${NC}"
echo "Testando vários tipos MIME simultaneamente..."
echo ""

echo "  4.1) HTML (index.html)"
ab -n 500 -c 25 "$SERVER_URL/index.html" > /tmp/ab_html.log 2>&1
FAILED_HTML=$(grep "Failed requests:" /tmp/ab_html.log | awk '{print $3}')
echo "      Falhas: $FAILED_HTML"

echo "  4.2) CSS (style.css)"
ab -n 500 -c 25 "$SERVER_URL/style.css" > /tmp/ab_css.log 2>&1
FAILED_CSS=$(grep "Failed requests:" /tmp/ab_css.log | awk '{print $3}')
echo "      Falhas: $FAILED_CSS"

echo "  4.3) JS (script.js)"
ab -n 500 -c 25 "$SERVER_URL/script.js" > /tmp/ab_js.log 2>&1
FAILED_JS=$(grep "Failed requests:" /tmp/ab_js.log | awk '{print $3}')
echo "      Falhas: $FAILED_JS"

TOTAL_FAILED=$((FAILED_HTML + FAILED_CSS + FAILED_JS))
if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ PASS: Todos os tipos MIME testados sem falhas!${NC}"
else
    echo -e "${RED}✗ FAIL: Total de $TOTAL_FAILED falhas no mix de ficheiros!${NC}"
fi
echo ""

# ==========================================
# TESTE 5: Keep-Alive vs Close
# ==========================================
echo -e "${BLUE}TESTE 5: Comparação Keep-Alive vs Connection Close${NC}"
echo "Nota: O servidor usa 'Connection: close' por defeito (HTTP/1.1 sem keep-alive)"
echo ""

ab -n 500 -c 10 -k "$SERVER_URL/index.html" > /tmp/ab_keepalive.log 2>&1
RPS_KA=$(grep "Requests per second:" /tmp/ab_keepalive.log | awk '{print $4}')

ab -n 500 -c 10 "$SERVER_URL/index.html" > /tmp/ab_close.log 2>&1
RPS_CLOSE=$(grep "Requests per second:" /tmp/ab_close.log | awk '{print $4}')

echo "  Keep-Alive (-k): $RPS_KA req/sec"
echo "  Close (padrão): $RPS_CLOSE req/sec"
echo -e "${GREEN}✓ Comparação completa${NC}"
echo ""

# ==========================================
# RESUMO FINAL
# ==========================================
echo "=========================================="
echo "   RESUMO DOS TESTES DE CARGA"
echo "=========================================="
echo ""

TOTAL_REQUESTS=$((100 + 1000 + 10000 + 500 + 500 + 500))
TOTAL_FAILED=$((FAILED1 + FAILED2 + FAILED3 + TOTAL_FAILED))

echo "Total de pedidos enviados: $TOTAL_REQUESTS"
echo "Total de falhas: $TOTAL_FAILED"
echo ""

if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓✓✓ SUCESSO: Servidor aguentou TODOS os testes de carga!${NC}"
    echo ""
    echo "Métricas de Performance:"
    echo "  - Carga Leve: $RPS1 req/sec"
    echo "  - Carga Média: $RPS2 req/sec"
    echo "  - Carga Pesada: $RPS3 req/sec"
    echo ""
    echo -e "${YELLOW}DICA: Verifique as estatísticas do servidor (stdout) para confirmar${NC}"
    echo "       que os contadores batem certo com o Apache Bench!"
    exit 0
else
    echo -e "${RED}✗✗✗ FALHA: $TOTAL_FAILED pedidos falharam!${NC}"
    echo ""
    echo "Verifique:"
    echo "  1. Logs do servidor (erros no terminal)"
    echo "  2. Recursos do sistema (memória/CPU)"
    echo "  3. Ficheiros de log: /tmp/ab_test*.log"
    exit 1
fi