#ifndef IO_CONFIGS_H_
#define IO_CONFIGS_H_

#include <stdbool.h>
#include <commons/log.h>

typedef struct {
	char* log_level;
	char* scheduler_ip;
	char* scheduler_port;
	char* io_name;      // nombre de la interfaz (STDIN / STDOUT / SLEEP / etc.)
} t_config_io;

extern t_config_io* config_io;
extern t_log* logger;

// Carga la config desde el archivo y guarda el nombre de la IO
bool load_config_io(const char* path, const char* io_name);

// Inicializa el logger de IO según LOG_LEVEL
bool init_logger_io(void);

// Libera la config y el logger
void free_config_io(void);

#endif // IO_CONFIGS_H_
