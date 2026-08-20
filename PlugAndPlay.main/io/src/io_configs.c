#include "io_configs.h"

#include <commons/config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

t_config_io* config_io = NULL;
t_log* logger = NULL;

bool load_config_io(const char* path, const char* io_name) {
	t_config* cfg = config_create(path);
	if (cfg == NULL) {
		return false;
	}

	config_io = malloc(sizeof(t_config_io));
	if (config_io == NULL) {
		config_destroy(cfg);
		return false;
	}

	if (!config_has_property(cfg, "LOG_LEVEL") ||
		!config_has_property(cfg, "SCHEDULER_IP") ||
		!config_has_property(cfg, "SCHEDULER_PORT")) {
		config_destroy(cfg);
		free(config_io);
		config_io = NULL;
		return false;
	}

	config_io->log_level   = strdup(config_get_string_value(cfg, "LOG_LEVEL"));
	config_io->scheduler_ip   = strdup(config_get_string_value(cfg, "SCHEDULER_IP"));
	config_io->scheduler_port = strdup(config_get_string_value(cfg, "SCHEDULER_PORT"));
	config_io->io_name = strdup(io_name);

	config_destroy(cfg);
	return true;
}

bool init_logger_io(void) {
	if (config_io == NULL || config_io->log_level == NULL || config_io->io_name == NULL) {
		return false;
	}

	t_log_level level = log_level_from_string(config_io->log_level);

	char log_path[256];
	snprintf(log_path, sizeof(log_path), "io_%s.log", config_io->io_name);

	logger = log_create(
		log_path,
		config_io->io_name,
		true,
		level
	);

	return (logger != NULL);
}

void free_config_io(void) {
	if (config_io != NULL) {
		free(config_io->log_level);
		free(config_io->scheduler_ip);
		free(config_io->scheduler_port);
		free(config_io->io_name);
		free(config_io);
		config_io = NULL;
	}

	if (logger != NULL) {
		log_destroy(logger);
		logger = NULL;
	}
}
