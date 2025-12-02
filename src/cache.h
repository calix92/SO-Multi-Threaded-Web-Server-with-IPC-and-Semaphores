// cache.h
#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>

typedef struct cache_entry {
    char* key;
    void* data;
    size_t size;
    struct cache_entry* next;
    struct cache_entry* prev;
} cache_entry_t;

typedef struct {
    cache_entry_t* head;
    cache_entry_t* tail;
    pthread_rwlock_t lock;
    size_t max_size;
    size_t current_size;
} cache_t;

cache_t* cache_init(size_t max_size_mb);
cache_entry_t* cache_get(cache_t* cache, const char* key);
void cache_put(cache_t* cache, const char* key, void* data, size_t size);
void cache_destroy(cache_t* cache);

#endif