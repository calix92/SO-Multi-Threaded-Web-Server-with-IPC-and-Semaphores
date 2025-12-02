#include "stats.h"
#include <stdio.h>

void update_stats(shared_data_t* data, semaphores_t* sems, 
                  int status, size_t bytes) {
    sem_wait(sems->stats_mutex);
    
    data->stats.total_requests++;
    data->stats.bytes_transferred += bytes;
    
    if (status == 200) data->stats.status_200++;
    else if (status == 404) data->stats.status_404++;
    else if (status == 500) data->stats.status_500++;
    
    sem_post(sems->stats_mutex);
}

void display_stats(shared_data_t* data, semaphores_t* sems) {
    sem_wait(sems->stats_mutex);
    
    printf("\n========================================\n");
    printf("SERVER STATISTICS\n");
    printf("========================================\n");
    printf("Total Requests: %ld\n", data->stats.total_requests);
    printf("Bytes Transferred: %ld\n", data->stats.bytes_transferred);
    printf("Status 200: %ld\n", data->stats.status_200);
    printf("Status 404: %ld\n", data->stats.status_404);
    printf("Status 500: %ld\n", data->stats.status_500);
    printf("Active Connections: %d\n", data->stats.active_connections);
    printf("========================================\n\n");
    
    sem_post(sems->stats_mutex);
}