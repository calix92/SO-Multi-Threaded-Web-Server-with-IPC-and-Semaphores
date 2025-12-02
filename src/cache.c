// cache.c - Estrutura básica
#include "cache.h"
#include <stdlib.h>

cache_t* cache_init(size_t max_size_mb) {
    // TODO: Implementar
    return NULL;
}

cache_entry_t* cache_get(cache_t* cache, const char* key) {
    // TODO: Implementar busca na cache
    // - Fazer lock de leitura (pthread_rwlock_rdlock)
    // - Procurar entrada
    // - Mover para head (LRU)
    // - Fazer unlock
    return NULL;
}

void cache_put(cache_t* cache, const char* key, void* data, size_t size) {
    // TODO: Implementar inserção na cache
    // - Fazer lock de escrita (pthread_rwlock_wrlock)
    // - Verificar se já existe
    // - Se cache cheia, remover LRU
    // - Inserir nova entrada
    // - Fazer unlock
}

void cache_destroy(cache_t* cache) {
    // TODO: Implementar destruição da cache
    // - Libertar todas as entradas
    // - Destruir rwlock
    // - Libertar estrutura
}