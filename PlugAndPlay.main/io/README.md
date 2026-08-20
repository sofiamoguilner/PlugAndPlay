# IO

Módulo del TP **Plug & Pray** que simula las interfaces de Entrada/Salida del sistema. Cumple el rol descripto en el enunciado (págs. 26-27): un proceso que se conecta al Kernel Scheduler y atiende pedidos de IO de los Procesos planificados.

Un mismo binario `bin/io` puede levantarse con distintos tipos según el segundo argumento de la CLI: **STDIN**, **STDOUT** o **SLEEP**.

---

## Qué hace

1. **Se identifica como IO de un tipo específico.** Al arrancar, recibe el "nombre/tipo" como argumento (`STDIN`, `STDOUT`, `SLEEP`) y se lo manda al Kernel Scheduler en el handshake.
2. **Se conecta al Kernel Scheduler.** Establece la conexión y queda a la espera de pedidos.
3. **Atiende pedidos según el tipo** (un loop infinito por cada `op_code` recibido):
   - **`IO_SLEEP`**: recibe `(PID, ms)`, hace `usleep(ms * 1000)` y avisa al Scheduler.
   - **`IO_STDIN`**: recibe `(PID, cantidad)`, pide al usuario que tipee, lee desde teclado, trunca o rellena con `\0` para que el output tenga exactamente `cantidad` bytes, y devuelve el buffer al Scheduler.
   - **`IO_STDOUT`**: recibe `(PID, mensaje)`, lo imprime al log y avisa al Scheduler que terminó.
4. **Notifica fin de IO** con el op_code `IO_FINALIZADA`, así el Scheduler puede sacar al Proceso de `BLOCK` y volverlo a `READY`.

---

## Cómo lo hace

### Estructura del módulo

```
src/
├── main.c               # Loop principal: dispatcher de IO_SLEEP / IO_STDIN / IO_STDOUT
├── io_configs.{c,h}     # Carga del .conf + logger
└── io_conexiones.{c,h}  # Handshake con Scheduler + helpers de respuesta
```

### Flujo de arranque ([main.c](src/main.c))

1. Valida argumentos: `./bin/io <config_path> <io_name>`.
2. `load_config_io(path, io_name)` → carga `SCHEDULER_IP`, `SCHEDULER_PORT`, `LOG_LEVEL` y guarda `io_name`.
3. Inicializa el logger con el `io_name` como nombre lógico (aparece en cada línea).
4. `conectar_a_scheduler(...)` → conexión TCP + handshake `HANDSHAKE_IO + io_name`.
5. Loopea: `recibir_codigo_operacion(scheduler_socket)` y despacha por `switch` al handler correspondiente.

### Dispatcher de operaciones

El loop principal de [main.c:44-153](src/main.c#L44-L153) demultiplexa por `op_code`:

| Op | Wire format recibido | Comportamiento | Respuesta |
|---|---|---|---|
| `IO_SLEEP` | `pid:int`, `tiempo_ms:int` | `usleep(ms*1000)` | `IO_FINALIZADA` con PID |
| `IO_STDIN` | `pid:int`, `cantidad:int` | `fgets()` de stdin, recorta `\n`, trunca/rellena a `cantidad` | `IO_FINALIZADA` con PID + buffer |
| `IO_STDOUT` | `pid:int`, `mensaje:bytes` | `log_info(... %s ...)` | `IO_FINALIZADA` con PID |

### STDIN — detalles

[main.c:77-119](src/main.c#L77-L119):
- Reserva un `calloc(cantidad)` (inicializado en `\0` → padding automático).
- Lee con `fgets(linea, 4096, stdin)`.
- Quita el `\n` final si existe.
- `memcpy(buffer, linea, min(len, cantidad))` → si el usuario tipea más caracteres que los pedidos, se trunca; si tipea menos, los restantes quedan en `\0` por el `calloc`.

### STDOUT — detalles

[main.c:122-147](src/main.c#L122-L147):
- El tamaño del mensaje viene implícito en el size del paquete (`size - sizeof(pid)`).
- Reserva `buffer_size + 1` y agrega `\0` final para usar `%s` de forma segura.

### Notificación al Scheduler ([io_conexiones.c](src/io_conexiones.c))

Dos helpers:
- `enviar_fin_io(pid)`: paquete `IO_FINALIZADA` con sólo el PID (usado por SLEEP y STDOUT).
- `enviar_datos_stdin(pid, buffer, size)`: paquete `IO_FINALIZADA` con PID + buffer (usado por STDIN).

El Scheduler distingue el caso STDIN porque hay payload extra después del PID.

---

## Configuración

Archivo: [`io.conf`](io.conf)

| Campo | Tipo | Descripción |
|---|---|---|
| `LOG_LEVEL` | string | Compatible con `log_level_from_string()` |
| `SCHEDULER_IP` | string | IP del Kernel Scheduler |
| `SCHEDULER_PORT` | string | Puerto del Kernel Scheduler |

El **tipo de IO** (`STDIN`, `STDOUT`, `SLEEP`) **no** está en el config: se pasa como segundo argumento de la CLI. Eso permite levantar múltiples instancias con el mismo `.conf` y distintos roles.

---

## Uso

```bash
make
./bin/io io.conf SLEEP
./bin/io io.conf STDIN
./bin/io io.conf STDOUT
```

Para levantar varias IO en paralelo, abrir terminales distintas (las STDIN/STDOUT necesitan tty para `fgets` y para que se vea el output al usuario).

---

## Logs obligatorios (formato exacto del enunciado)

| Evento | Formato |
|---|---|
| Conexión a Scheduler | `## Conectado a Kernel Scheduler` |
| Inicio de IO | `## PID: <PID> - Inicio de IO` |
| Fin de IO | `## PID: <PID> - Fin de IO` |
| Solo STDOUT | `## PID: <PID> - <CONTENIDO A IMPRIMIR>` |
| Solo STDIN | `## PID: <PID> - Ingrese <CANTIDAD A LEER> caracteres:` |
| Solo SLEEP | `## PID: <PID> - Haciendo sleep por <TIEMPO> milisegundos.` |

Salida en consola + archivo `io.log` (compartido por todas las instancias; el `io_name` aparece como prefijo lógico).
