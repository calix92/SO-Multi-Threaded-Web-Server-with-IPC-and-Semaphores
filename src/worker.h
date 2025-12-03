#ifndef WORKER_H
#define WORKER_H

// Recebe apenas o ID, não precisa do socket porque vai ler da SHM
void worker_main(int worker_id);

#endif