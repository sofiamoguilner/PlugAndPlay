#ifndef SOCKETS_H_
#define SOCKETS_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/collections/list.h>
#include <stddef.h>

// Estructuras de paquete "legacy" usadas solo por IO y helpers
// de bajo nivel. No deben confundirse con los tipos de serializacion.h.
typedef struct {
    int size;
    void* stream;
} t_socket_buffer;

typedef struct {
    int codigo_operacion;
    t_socket_buffer* buffer;
} t_socket_paquete;

#define MENSAJE 0
#define PAQUETE 1

extern t_log* LOGGER;

int start_server(char* ip, char* puerto);
int create_connection(char* ip, char* puerto);
void connection_listener(char* name, int server_socket, void* (*handler)(void*));
void release_connection(int* socket_cliente);
int iniciar_servidor(const char* puerto);
int esperar_cliente(int socket_servidor);
int recibir_operacion(int socket_cliente);
void* recibir_buffer(int* size, int socket_cliente);
char* recibir_mensaje(int socket_cliente);
t_list* recibir_paquete(int socket_cliente);
void* socket_serializar_paquete(t_socket_paquete* paquete, int bytes);
int crear_conexion(char *ip, char* puerto);
void enviar_mensaje(char* mensaje, int socket_cliente);
void socket_crear_buffer(t_socket_paquete* paquete);
t_socket_paquete* socket_crear_paquete(void);
void socket_agregar_a_paquete(t_socket_paquete* paquete, void* valor, int tamanio);
int socket_enviar_paquete(t_socket_paquete* paquete, int socket_cliente);
void socket_eliminar_paquete(t_socket_paquete* paquete);
void liberar_conexion(int socket_cliente);
int socket_enviar_paquete_simple(int socket_fd, int opcode, char* instr);
int split_file_tag(const char* filetag, char** out_file, char** out_tag);

// Helpers genéricos para enviar/recibir exactamente len bytes
int recv_all(int socket_fd, void* buffer, size_t len);
int send_all(int socket_fd, const void* buffer, size_t len);

#endif