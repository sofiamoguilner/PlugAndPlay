#ifndef MEMORY_IO_H
#define MEMORY_IO_H

#include <stdint.h>

// Leer/escribir cross-stick a la RAM física distribuida.
// Caller NO debe tener mutex_memoria ni ningún outbound_lock.
//
// memory_io_leer retorna un buffer malloc'd (caller hace free), NULL si error.
// memory_io_escribir retorna 0 si ok, -1 si error.
// En caso de desconexión de un stick durante la operación, envía BSOD al scheduler.
// pid se propaga al Memory Stick para que loguee el acceso (formato enunciado).
// Para operaciones del sistema (compactación, etc.) pasar pid=-1.
void* memory_io_leer(int pid, uint32_t dir_fisica, uint32_t tamanio);
int   memory_io_escribir(int pid, uint32_t dir_fisica, uint32_t tamanio, void* datos);

#endif
