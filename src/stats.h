// src/stats.h
#ifndef STATS_H
#define STATS_H

#include "shared_mem.h"
#include "semaphores.h"

void update_stats(shared_data_t* data, semaphores_t* sems, 
                  int status, size_t bytes, long response_time_ms, int is_cache_hit);

void display_stats(shared_data_t* data, semaphores_t* sems);

#endif