#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <commons/collections/list.h>
#include <string.h>
#include "io_configs.h"
#include "io_conexiones.h"
#include <utils/sockets.h>
#include <utils/serializacion.h>

int main(int argc, char** argv) {
   
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <config_path> <io_type>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* config_path = argv[1];
    const char* io_name     = argv[2];

    /*const char* config_path = "/home/utnso/tp-2026-1c-La-Serpiente-Shnissugah/io/io.conf"; //argv[1];
    const char* io_name     = "sleep"; //argv[2];*/

    // Cargar config 
    if (!load_config_io(config_path, io_name)) {
        fprintf(stderr, "No se pudo cargar la config de IO (%s)\n", config_path);
        return EXIT_FAILURE;
    }

    // Inicializar logger
    if (!init_logger_io()) {
        fprintf(stderr, "No se pudo inicializar el logger de IO\n");
        free_config_io();
        return EXIT_FAILURE;
    }

    // Conectar al Kernel S, log obligatorio dentro
    if (!conectar_a_scheduler(config_io->scheduler_ip, config_io->scheduler_port)) {
        log_error(logger, "No se pudo conectar al Kernel Scheduler");
        free_config_io();
        return EXIT_FAILURE;
    }

    // Loop principal: esperar operaciones del Scheduler
    while (1) {
        op_code opcode = recibir_codigo_operacion(scheduler_socket);
        if ((int)opcode == -1) {
            log_warning(logger, "Kernel Scheduler desconectado, cerrando IO");
            break;
        }

        switch (opcode) {

            // IO_SLEEP: recibe PID + tiempo en milisegundos
            case IO_SLEEP: {
                size_t size;
                recv(scheduler_socket, &size, sizeof(size_t), MSG_WAITALL);
                int pid, tiempo_ms;
                recv(scheduler_socket, &pid,       sizeof(int), MSG_WAITALL);
                recv(scheduler_socket, &tiempo_ms, sizeof(int), MSG_WAITALL);

                // Logs obligatorios
                log_info(logger, "## PID: %d - Inicio de IO", pid);
                log_info(logger, "## PID: %d - Haciendo sleep por %d milisegundos.", pid, tiempo_ms);

                if (tiempo_ms > 0) {
                    // usleep() es UB con valores >= 1.000.000us, por eso splitteo
                    sleep((unsigned int)(tiempo_ms / 1000));
                    usleep((unsigned int)(tiempo_ms % 1000) * 1000);
                }

                log_info(logger, "## PID: %d - Fin de IO", pid);

                enviar_fin_io(scheduler_socket, pid);
                break;
            }

            // IO_STDIN: recibe PID + cantidad de bytes a leer
            // Lee del teclado, recorta o rellena con \0, devuelve al Scheduler
            case IO_STDIN: {
                size_t size;
                recv(scheduler_socket, &size, sizeof(size_t), MSG_WAITALL);
                int pid, cantidad;
                recv(scheduler_socket, &pid,      sizeof(int), MSG_WAITALL);
                recv(scheduler_socket, &cantidad, sizeof(int), MSG_WAITALL);

                // Logs obligatorios
                log_info(logger, "## PID: %d - Inicio de IO", pid);
                log_info(logger, "## PID: %d - Ingrese %d caracteres:", pid, cantidad);

                // Buffer exacto del tamaño pedido, inicializado en \0
                char* buffer = calloc(cantidad, sizeof(char));
                if (buffer == NULL) {
                    log_error(logger, "No se pudo alocar buffer para IO_STDIN");
                    break;
                }

                // Leer línea del usuario
                char linea[4096];
                if (fgets(linea, sizeof(linea), stdin) != NULL) {
                    // Sacar el \n si está al final
                    size_t len = strlen(linea);
                    if (len > 0 && linea[len - 1] == '\n') {
                        linea[len - 1] = '\0';
                        len--;
                    }
                    // Copiar solo cant bytes (corta si es mayor, el resto queda en \0)
                    memcpy(buffer, linea, len < (size_t)cantidad ? len : (size_t)cantidad);
                } else {
                    log_warning(logger, "Error leyendo de STDIN para PID %d", pid);
                }

                log_info(logger, "## PID: %d - Fin de IO", pid);

                // Enviar datos leidos al Scheduler
                enviar_datos_stdin(scheduler_socket, pid, buffer, cantidad);

                free(buffer);
                break;
            }

            // IO_STDOUT: recibe PID + buffer a imprimir
            case IO_STDOUT: {
                size_t size;
                recv(scheduler_socket, &size, sizeof(size_t), MSG_WAITALL);
                int pid;
                recv(scheduler_socket, &pid, sizeof(int), MSG_WAITALL);
                // El buffer viene sin prefix de longitud, solo los bytes directos (size - sizeof(pid))
                int buffer_size = (int)(size - sizeof(int));
                char* mensaje = malloc(buffer_size + 1);
                if (mensaje == NULL) {
                    log_error(logger, "No se pudo alocar buffer para IO_STDOUT");
                    break;
                }
                memset(mensaje, 0, buffer_size + 1);
                recv(scheduler_socket, mensaje, buffer_size, MSG_WAITALL);

                // Logs obligatorios
                log_info(logger, "## PID: %d - Inicio de IO", pid);
                log_info(logger, "## PID: %d - %s", pid, mensaje);
                log_info(logger, "## PID: %d - Fin de IO", pid);
                free(mensaje);

                enviar_fin_io(scheduler_socket, pid);
                break;
            }

            default:
                log_warning(logger, "Opcode desconocido recibido en IO: %d", opcode);
                break;
        }
    }

    free_config_io();
    return EXIT_SUCCESS;
}
