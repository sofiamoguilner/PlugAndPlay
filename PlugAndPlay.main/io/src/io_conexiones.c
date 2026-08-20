#include "io_conexiones.h"
#include "io_configs.h"
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include <utils/serializacion.h>
#include <commons/log.h>
#include <sys/socket.h>

int scheduler_socket = -1;


// CONEXIÓN A SCHEDULER

bool conectar_a_scheduler(char* ip, char* puerto)
{
    log_debug(logger, "Intentando conectar a Kernel Scheduler en %s:%s", ip, puerto);

    scheduler_socket = crear_conexion(ip, puerto);
    if (scheduler_socket == -1) {
        log_error(logger, "No se pudo conectar al Kernel Scheduler");
        return false;
    }

    // Handshake de identificación como módulo IO (opcode + nombre como cadena)
    enviar_handshake_io(scheduler_socket, config_io->io_name);

    // Log obligatorio
    log_info(logger, "## Conectado a Kernel Scheduler");
    return true;
}
