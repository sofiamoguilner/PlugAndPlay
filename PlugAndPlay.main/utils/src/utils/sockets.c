#define _POSIX_C_SOURCE 200112L

#include "sockets.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>


int iniciar_servidor(const char* puerto) {

    int socket_servidor;

    struct addrinfo hints;
    struct addrinfo* servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, puerto, &hints, &servinfo);

    socket_servidor = socket(servinfo->ai_family,
                             servinfo->ai_socktype,
                             servinfo->ai_protocol);

    // Más portable que REUSEPORT
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    int err = 0;
    err = bind(socket_servidor, servinfo->ai_addr, servinfo->ai_addrlen);
    err = listen(socket_servidor, SOMAXCONN);

    if (err != 0)
        fprintf(stderr, "Error con el puerto %s\n", puerto);

    freeaddrinfo(servinfo);

    return socket_servidor;
}

int esperar_cliente(int socket_servidor) {
    return accept(socket_servidor, NULL, NULL);
}

int recibir_operacion(int socket_cliente) {
    int cod_op;

    if (recv(socket_cliente, &cod_op, sizeof(int), MSG_WAITALL) > 0)
        return cod_op;

    close(socket_cliente);
    return -1;
}

void* recibir_buffer(int* size, int socket_cliente) {
    void* buffer;

    recv(socket_cliente, size, sizeof(int), MSG_WAITALL);
    buffer = malloc(*size);
    recv(socket_cliente, buffer, *size, MSG_WAITALL);

    return buffer;
}

char* recibir_mensaje(int socket_cliente) {
    int size = 0;
    char* buffer = recibir_buffer(&size, socket_cliente);
    return buffer; // hacer free afuera
}

t_list* recibir_paquete(int socket_cliente) {
    int size;
    int desplazamiento = 0;
    void* buffer;
    t_list* valores = list_create();
    int tamanio;

    buffer = recibir_buffer(&size, socket_cliente);

    while (desplazamiento < size) {
        memcpy(&tamanio, buffer + desplazamiento, sizeof(int));
        desplazamiento += sizeof(int);

        char* valor = malloc(tamanio);
        memcpy(valor, buffer + desplazamiento, tamanio);
        desplazamiento += tamanio;

        list_add(valores, valor);
    }

    free(buffer);
    return valores;
}

void ensure(bool cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "%s\n", msg);
        exit(EXIT_FAILURE);
    }
}

int recv_all(int socket_fd, void* buffer, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t rc = recv(socket_fd, (char*)buffer + received, len - received, MSG_WAITALL);
        if (rc <= 0) {
            return -1;
        }
        received += (size_t)rc;
    }
    return 0;
}

int send_all(int socket_fd, const void* buffer, size_t len) {
    if (getenv("UTILS_TRACE_WIRE") != NULL) {
        char hex[3 * 16 + 1] = {0};
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++) {
            snprintf(hex + i * 3, 4, "%02x ", ((const unsigned char*)buffer)[i]);
        }
        fprintf(stderr, "[wire-send fd=%d len=%zu] %s\n", socket_fd, len, hex);
    }

    size_t sent = 0;
    while (sent < len) {
        ssize_t rc = send(socket_fd, (const char*)buffer + sent, len - sent, 0);
        if (rc <= 0) {
            return -1;
        }
        sent += (size_t)rc;
    }
    return 0;
}


void* socket_serializar_paquete(t_socket_paquete* paquete, int bytes) {
    void* stream = malloc(bytes);
    int offset = 0;

    memcpy(stream + offset, &(paquete->codigo_operacion), sizeof(int));
    offset += sizeof(int);

    memcpy(stream + offset, &(paquete->buffer->size), sizeof(int));
    offset += sizeof(int);

    memcpy(stream + offset, paquete->buffer->stream, paquete->buffer->size);

    return stream;
}

int crear_conexion(char* ip, char* puerto) {

    struct addrinfo hints;
    struct addrinfo* server_info;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(ip, puerto, &hints, &server_info);

    int socket_cliente = socket(server_info->ai_family,
                                server_info->ai_socktype,
                                server_info->ai_protocol);

    if (connect(socket_cliente, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        printf("Conexion fallida\n");
        freeaddrinfo(server_info);
        return -1;
    }

    freeaddrinfo(server_info);
    return socket_cliente;
}

void enviar_mensaje(char* mensaje, int socket_cliente) {
    t_socket_paquete* paquete = malloc(sizeof(t_socket_paquete));

    paquete->codigo_operacion = MENSAJE;
    paquete->buffer = malloc(sizeof(t_socket_buffer));

    paquete->buffer->size = strlen(mensaje) + 1;
    paquete->buffer->stream = malloc(paquete->buffer->size);

    memcpy(paquete->buffer->stream, mensaje, paquete->buffer->size);

    int bytes = paquete->buffer->size + 2 * sizeof(int);
    void* a_enviar = socket_serializar_paquete(paquete, bytes);

    if (send_all(socket_cliente, a_enviar, (size_t)bytes) != 0) {
        fprintf(stderr, "Error enviando mensaje completo\n");
    }

    free(a_enviar);
    socket_eliminar_paquete(paquete);
}

void socket_crear_buffer(t_socket_paquete* paquete) {
    paquete->buffer = malloc(sizeof(t_socket_buffer));
    paquete->buffer->size = 0;
    paquete->buffer->stream = NULL;
}

t_socket_paquete* socket_crear_paquete(void) {
    t_socket_paquete* paquete = malloc(sizeof(t_socket_paquete));
    paquete->codigo_operacion = PAQUETE;
    socket_crear_buffer(paquete);
    return paquete;
}

void socket_agregar_a_paquete(t_socket_paquete* paquete, void* valor, int tamanio) {
    paquete->buffer->stream = realloc(
        paquete->buffer->stream,
        paquete->buffer->size + tamanio + sizeof(int)
    );

    memcpy(paquete->buffer->stream + paquete->buffer->size, &tamanio, sizeof(int));
    memcpy(paquete->buffer->stream + paquete->buffer->size + sizeof(int), valor, tamanio);

    paquete->buffer->size += tamanio + sizeof(int);
}

int socket_enviar_paquete(t_socket_paquete* paquete, int socket_cliente) {
    int bytes = paquete->buffer->size + 2 * sizeof(int);

    void* a_enviar = socket_serializar_paquete(paquete, bytes);
    int resultado = send_all(socket_cliente, a_enviar, (size_t)bytes);

    free(a_enviar);
    return resultado;
}

void socket_eliminar_paquete(t_socket_paquete* paquete) {
    free(paquete->buffer->stream);
    free(paquete->buffer);
    free(paquete);
}

void liberar_conexion(int socket_cliente) {
    close(socket_cliente);
}

int socket_enviar_paquete_simple(int socket_fd, int opcode, char* instr) {
    t_socket_paquete* p = socket_crear_paquete();
    p->codigo_operacion = opcode;

    socket_agregar_a_paquete(p, instr, strlen(instr) + 1);

    socket_enviar_paquete(p, socket_fd);
    socket_eliminar_paquete(p);

    return 0;
}

// helper "<file:tag>"
int split_file_tag(const char* filetag, char** out_file, char** out_tag) {

    const char* sep = strchr(filetag, ':');
    if (!sep) return -1;

    size_t len_file = sep - filetag;
    size_t len_tag = strlen(sep + 1);

    char* f = malloc(len_file + 1);
    char* t = malloc(len_tag + 1);

    memcpy(f, filetag, len_file);
    f[len_file] = '\0';

    memcpy(t, sep + 1, len_tag + 1);

    *out_file = f;
    *out_tag = t;

    return 0;
}