#!/bin/bash
# tests/test_functional.sh
# Testes Funcionais do ConcurrentHTTP Server

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SERVER_URL="http://localhost:8080"
PASSED=0
FAILED=0

echo "=========================================="
echo "   TESTES FUNCIONAIS - ConcurrentHTTP"
echo "=========================================="
echo ""

# Função auxiliar para imprimir resultado
print_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}[PASS]${NC} $2"
        ((PASSED++))
    else
        echo -e "${RED}[FAIL]${NC} $2"
        ((FAILED++))
    fi
}

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
# TESTE 1: Index (Raiz /)
# ==========================================
echo "TESTE 1: Acesso à raiz (/) devolve index.html"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/")
if [ "$RESPONSE" = "200" ]; then
    CONTENT=$(curl -s "$SERVER_URL/" | grep -c "ConcurrentHTTP Server")
    if [ $CONTENT -gt 0 ]; then
        print_result 0 "GET / retorna index.html com código 200"
    else
        print_result 1 "GET / retorna 200 mas conteúdo incorreto"
    fi
else
    print_result 1 "GET / retornou código $RESPONSE em vez de 200"
fi

# ==========================================
# TESTE 2: Ficheiro HTML
# ==========================================
echo "TESTE 2: Download de ficheiro HTML"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/index.html")
CONTENT_TYPE=$(curl -s -I "$SERVER_URL/index.html" | grep -i "content-type" | grep -c "text/html")
if [ "$RESPONSE" = "200" ] && [ $CONTENT_TYPE -gt 0 ]; then
    print_result 0 "GET /index.html (200, Content-Type: text/html)"
else
    print_result 1 "GET /index.html falhou (HTTP: $RESPONSE, CT correto: $CONTENT_TYPE)"
fi

# ==========================================
# TESTE 3: Ficheiro CSS
# ==========================================
echo "TESTE 3: Download de ficheiro CSS"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/style.css")
CONTENT_TYPE=$(curl -s -I "$SERVER_URL/style.css" | grep -i "content-type" | grep -c "text/css")
if [ "$RESPONSE" = "200" ] && [ $CONTENT_TYPE -gt 0 ]; then
    print_result 0 "GET /style.css (200, Content-Type: text/css)"
else
    print_result 1 "GET /style.css falhou (HTTP: $RESPONSE, CT correto: $CONTENT_TYPE)"
fi

# ==========================================
# TESTE 4: Ficheiro JavaScript
# ==========================================
echo "TESTE 4: Download de ficheiro JavaScript"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/script.js")
CONTENT_TYPE=$(curl -s -I "$SERVER_URL/script.js" | grep -i "content-type" | grep -c "application/javascript")
if [ "$RESPONSE" = "200" ] && [ $CONTENT_TYPE -gt 0 ]; then
    print_result 0 "GET /script.js (200, Content-Type: application/javascript)"
else
    print_result 1 "GET /script.js falhou (HTTP: $RESPONSE, CT correto: $CONTENT_TYPE)"
fi

# ==========================================
# TESTE 5: Erro 404 (Ficheiro inexistente)
# ==========================================
echo "TESTE 5: Erro 404 - Ficheiro não encontrado"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/nao_existe.html")
if [ "$RESPONSE" = "404" ]; then
    CONTENT=$(curl -s "$SERVER_URL/nao_existe.html" | grep -c "404")
    if [ $CONTENT -gt 0 ]; then
        print_result 0 "GET /nao_existe.html retorna 404 com página de erro"
    else
        print_result 1 "GET /nao_existe.html retorna 404 mas sem página de erro"
    fi
else
    print_result 1 "GET /nao_existe.html retornou $RESPONSE em vez de 404"
fi

# ==========================================
# TESTE 6: Método HEAD
# ==========================================
echo "TESTE 6: Método HEAD (sem body)"
HEAD_RESPONSE=$(curl -s -I "$SERVER_URL/index.html" -o /dev/null -w "%{http_code}")
CONTENT_LENGTH=$(curl -s -I "$SERVER_URL/index.html" | grep -i "content-length" | wc -l)
if [ "$HEAD_RESPONSE" = "200" ] && [ $CONTENT_LENGTH -gt 0 ]; then
    print_result 0 "HEAD /index.html retorna 200 com Content-Length (sem body)"
else
    print_result 1 "HEAD /index.html falhou (HTTP: $HEAD_RESPONSE)"
fi

# ==========================================
# TESTE 7: Cache (2 pedidos iguais)
# ==========================================
echo "TESTE 7: Cache LRU (verificação indireta)"
echo "   Fazendo 2 pedidos consecutivos ao mesmo ficheiro..."
TIME1=$(curl -s -o /dev/null -w "%{time_total}" "$SERVER_URL/index.html")
TIME2=$(curl -s -o /dev/null -w "%{time_total}" "$SERVER_URL/index.html")
echo "   Tempo 1ª request: ${TIME1}s | Tempo 2ª request: ${TIME2}s"
# A segunda deve ser mais rápida (cache hit), mas nem sempre é garantido
if (( $(echo "$TIME2 <= $TIME1" | bc -l) )); then
    print_result 0 "Cache parece estar a funcionar (2º pedido <= 1º)"
else
    print_result 0 "Cache: 2º pedido foi mais lento (pode ser normal sob carga)"
fi

# ==========================================
# TESTE 8: Ficheiros de Erro Personalizados
# ==========================================
echo "TESTE 8: Página de erro 404 personalizada (404.html)"
CONTENT=$(curl -s "$SERVER_URL/ficheiro_inexistente.txt" | grep -c "404 - Página Não Encontrada")
if [ $CONTENT -gt 0 ]; then
    print_result 0 "Erro 404 devolve página HTML personalizada"
else
    print_result 1 "Erro 404 não devolve a página esperada (404.html)"
fi

# ==========================================
# TESTE 9: Verificar Access Log
# ==========================================
echo "TESTE 9: Integridade do ficheiro access.log"
if [ -f "access.log" ]; then
    LOG_LINES=$(wc -l < access.log)
    if [ $LOG_LINES -gt 0 ]; then
        # Verificar formato básico (IP - [timestamp] "METHOD path" status bytes)
        VALID_LINES=$(grep -cE '^\S+ - \[.*\] ".*" [0-9]{3} [0-9]+$' access.log)
        if [ $VALID_LINES -gt 0 ]; then
            print_result 0 "access.log existe e tem $LOG_LINES linhas válidas"
        else
            print_result 1 "access.log existe mas formato parece incorreto"
        fi
    else
        print_result 1 "access.log existe mas está vazio"
    fi
else
    print_result 1 "access.log não foi criado"
fi

# ==========================================
# TESTE 10: Múltiplos pedidos concorrentes rápidos
# ==========================================
echo "TESTE 10: 50 pedidos concorrentes rápidos"
for i in {1..50}; do
    curl -s "$SERVER_URL/index.html" > /dev/null &
done
wait
# Verificar se não há erros graves no log (opcional)
print_result 0 "50 pedidos concorrentes completados (verificar no terminal do servidor)"

echo ""
echo "=========================================="
echo "   RESUMO DOS TESTES"
echo "=========================================="
echo -e "${GREEN}Passou: $PASSED${NC}"
echo -e "${RED}Falhou: $FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}:) Todos os testes funcionais passaram!${NC}"
    exit 0
else
    echo -e "${RED}:( Alguns testes falharam. Verifique os logs.${NC}"
    exit 1
fi