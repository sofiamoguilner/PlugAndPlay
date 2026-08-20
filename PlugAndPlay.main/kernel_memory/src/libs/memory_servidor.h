#ifndef MEMORY_SERVIDOR_H
#define MEMORY_SERVIDOR_H

void iniciar_servidor_memory(char* puerto);

void registrar_swap(int client_socket);

void spawn_detached(void* (*fn)(void*), void* arg, const char* nombre);

#endif
