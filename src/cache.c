// src/cache.c
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Função auxiliar para libertar uma entrada
void free_entry(cache_entry_t* entry) {
    if (entry) {
        if (entry->key) free(entry->key);
        if (entry->data) free(entry->data);
        free(entry);
    }
}

cache_t* cache_init(size_t max_size_mb) {
    cache_t* cache = malloc(sizeof(cache_t));
    if (!cache) return NULL;

    cache->head = NULL;
    cache->tail = NULL;
    cache->max_size = max_size_mb * 1024 * 1024; // Converter MB para Bytes
    cache->current_size = 0;

    if (pthread_rwlock_init(&cache->lock, NULL) != 0) {
        free(cache);
        return NULL;
    }

    return cache;
}

void cache_destroy(cache_t* cache) {
    if (!cache) return;

    pthread_rwlock_wrlock(&cache->lock);
    
    cache_entry_t* current = cache->head;
    while (current) {
        cache_entry_t* next = current->next;
        free_entry(current);
        current = next;
    }

    pthread_rwlock_unlock(&cache->lock);
    pthread_rwlock_destroy(&cache->lock);
    free(cache);
}

// Move uma entrada para o início da lista (LRU: Most Recently Used)
// Nota: Assume que o lock de escrita já está adquirido!
void move_to_head(cache_t* cache, cache_entry_t* entry) {
    if (entry == cache->head) return; // Já é o primeiro

    // Remover da posição atual
    if (entry->prev) entry->prev->next = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    if (entry == cache->tail) cache->tail = entry->prev;

    // Inserir no início
    entry->next = cache->head;
    entry->prev = NULL;
    if (cache->head) cache->head->prev = entry;
    cache->head = entry;
    
    if (!cache->tail) cache->tail = entry;
}

cache_entry_t* cache_get(cache_t* cache, const char* key) {
    // Usamos Write Lock porque vamos mexer na lista (mover para a frente)
    // Se fosse só ler sem atualizar LRU, seria Read Lock.
    pthread_rwlock_wrlock(&cache->lock);

    cache_entry_t* current = cache->head;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Encontrado! Mover para o início (LRU)
            move_to_head(cache, current);
            pthread_rwlock_unlock(&cache->lock);
            return current;
        }
        current = current->next;
    }

    pthread_rwlock_unlock(&cache->lock);
    return NULL;
}

void cache_put(cache_t* cache, const char* key, void* data, size_t size) {
    pthread_rwlock_wrlock(&cache->lock);

    // 1. Verificar se já existe (atualizar)
    cache_entry_t* current = cache->head;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Atualizar dados
            cache->current_size -= current->size;
            free(current->data);
            
            current->data = malloc(size);
            memcpy(current->data, data, size);
            current->size = size;
            cache->current_size += size;
            
            move_to_head(cache, current);
            pthread_rwlock_unlock(&cache->lock);
            return;
        }
        current = current->next;
    }

    // 2. Verificar espaço (Eviction)
    while (cache->current_size + size > cache->max_size && cache->tail) {
        // Remover o último (LRU)
        cache_entry_t* old_tail = cache->tail;
        
        cache->current_size -= old_tail->size;
        
        if (old_tail->prev) old_tail->prev->next = NULL;
        cache->tail = old_tail->prev;
        if (cache->head == old_tail) cache->head = NULL;

        free_entry(old_tail);
    }

    // 3. Inserir novo no início
    cache_entry_t* new_entry = malloc(sizeof(cache_entry_t));
    new_entry->key = strdup(key);
    new_entry->data = malloc(size);
    memcpy(new_entry->data, data, size);
    new_entry->size = size;

    new_entry->next = cache->head;
    new_entry->prev = NULL;

    if (cache->head) cache->head->prev = new_entry;
    cache->head = new_entry;
    if (!cache->tail) cache->tail = new_entry;

    cache->current_size += size;

    pthread_rwlock_unlock(&cache->lock);
}