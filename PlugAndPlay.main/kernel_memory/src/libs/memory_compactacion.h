#ifndef MEMORY_COMPACTACION_H
#define MEMORY_COMPACTACION_H

#include <stdbool.h>

bool compactacion_en_progreso(void);
void compactacion_marcar_inicio(void);
void compactacion_marcar_fin(void);

// Caller DEBE tener mutex_memoria tomado.
// Llama a memory_io_* (que toman sus propios locks internos).
void compactar_memoria(void);

#endif
