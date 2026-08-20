#ifndef MEMORY_SCHEDULER_H
#define MEMORY_SCHEDULER_H

// =============================================================================
// CONEXIÓN
// =============================================================================

void registrar_scheduler(int client_socket);

// =============================================================================
// THREAD RECEPTOR
// =============================================================================

void* memory_receptor_scheduler_thread(void* arg);

// =============================================================================
// HANDLERS DE PEDIDOS SCHEDULER
// =============================================================================

void manejar_crear_proceso(int socket);
void manejar_actualizar_contexto_scheduler(int socket);
void manejar_obtener_espacio_libre(int socket);
void manejar_lectura_memoria(int socket);
void manejar_escritura_memoria(int socket);
void manejar_suspender_proceso(int socket);
void manejar_des_suspender_proceso(int socket);
void manejar_solicitar_segmento(int socket);
void manejar_eliminar_segmento(int socket);
void manejar_finalizar_proceso(int socket);

#endif
