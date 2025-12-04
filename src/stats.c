// src/stats.c
#include "stats.h"
#include <stdio.h>
#include <time.h> // Necessário para time()

void update_stats(shared_data_t* data, semaphores_t* sems, 
                  int status, size_t bytes, long response_time_ms, int is_cache_hit) {
    sem_wait(sems->stats_mutex);
    
    data->stats.total_requests++;
    data->stats.bytes_transferred += bytes;
    
    if (status == 200) data->stats.status_200++;
    else if (status == 404) data->stats.status_404++;
    else if (status == 403) data->stats.status_403++;
    else if (status == 500) data->stats.status_500++;
    
    // --- NOVOS DADOS ---
    data->stats.total_response_time_ms += response_time_ms;
    if (is_cache_hit) data->stats.cache_hits++;
    
    sem_post(sems->stats_mutex);
}

void display_stats(shared_data_t* data, semaphores_t* sems) {
    sem_wait(sems->stats_mutex);
    
    // --- CÁLCULOS ---
    time_t now = time(NULL);
    long uptime = now - data->stats.start_time;
    
    double avg_time = 0;
    if (data->stats.total_requests > 0)
        avg_time = (double)data->stats.total_response_time_ms / data->stats.total_requests;

    double hit_rate = 0;
    if (data->stats.total_requests > 0)
        hit_rate = ((double)data->stats.cache_hits / data->stats.total_requests) * 100.0;
    // ----------------

    printf("\n========================================\n");
    printf("SERVER STATISTICS\n");
    printf("========================================\n");
    printf("Uptime: %ld seconds\n", uptime);
    printf("Total Requests: %ld\n", data->stats.total_requests);
    printf("Bytes Transferred: %ld\n", data->stats.bytes_transferred);
    printf("Status 200: %ld\n", data->stats.status_200);
    printf("Status 403: %ld\n", data->stats.status_403);
    printf("Status 404: %ld\n", data->stats.status_404);
    printf("Status 500: %ld\n", data->stats.status_500);
    printf("Average Response Time: %.2f ms\n", avg_time);
    printf("Active Connections: %d\n", data->stats.active_connections);
    printf("Cache Hit Rate: %.1f%%\n", hit_rate);
    printf("========================================\n\n");
    
    sem_post(sems->stats_mutex);
}