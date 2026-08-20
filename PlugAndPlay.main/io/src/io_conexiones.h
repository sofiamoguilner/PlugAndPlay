#ifndef IO_CONEXIONES_H_
#define IO_CONEXIONES_H_

#include <stdbool.h>
#include <utils/utils.h>
#include <utils/sockets.h>
#include <utils/mensajes.h>
#include "io_configs.h"

// Socket global hacia el Scheduler
extern int scheduler_socket;

// Conecta la IO al Scheduler usando ip/puerto de config
bool conectar_a_scheduler(char* ip, char* puerto);

// Las notificaciones de fin de IO y de datos de STDIN se envían con los
// helpers centralizados de utils/mensajes.h:
//   enviar_fin_io(scheduler_socket, pid)
//   enviar_datos_stdin(scheduler_socket, pid, buffer, size)

#endif
