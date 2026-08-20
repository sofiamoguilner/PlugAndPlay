# SWAP

Módulo del TP **Plug & Pray** que persiste en disco la memoria de los Procesos suspendidos. Cumple el rol descripto en el enunciado (págs. 28-29): mantiene un único archivo dividido en bloques de tamaño fijo y atiende pedidos de lectura/escritura **bloque a bloque** que le envía el Kernel Memory.

No administra ninguna estructura propia de espacio libre: eso es responsabilidad del Kernel Memory. El SWAP es un "disco tonto" que simplemente lee y escribe bloques cuando se lo piden.

---

## Qué hace

1. **Abre (o crea) un archivo respaldo en disco** del tamaño definido por config (`SWAP_FILE_PATH`, `SWAP_FILE_SIZE`).
2. **Se conecta al Kernel Memory** y le manda en el handshake `(BLOCK_SIZE, SWAP_FILE_SIZE)` para que KM sepa cómo direccionar los bloques.
3. **Atiende pedidos** del Kernel Memory en loop:
   - **Lectura de bloque**: recibe número de bloque, devuelve `BLOCK_SIZE` bytes leídos.
   - **Escritura de bloque**: recibe número de bloque + contenido, escribe en el archivo.
4. **Maneja `SIGINT` con cierre controlado** (libera config, cierra socket y archivo).

---

## Cómo lo hace

### Estructura del módulo

```
src/
├── main.c             # Bootstrapping y orquestación
├── swap_config.{c,h}  # Carga del .conf + handler de SIGINT
└── utils_swap.{c,h}   # Archivo SWAP + conexión + atención de operaciones
```

### Flujo de arranque ([main.c](src/main.c))

1. Carga el config con `load_swap_config(argv[1])`. Valida que `block_size <= swap_file_size`.
2. Crea logger `swap.log`.
3. Registra handler de `SIGINT` para cierre controlado (una vez → cleanup + exit; dos veces → `abort`).
4. **Abre el archivo SWAP** con `swap_abrir_archivo`:
   - `open(path, O_RDWR | O_CREAT, 0644)`
   - `ftruncate(fd, swap_file_size)` → garantiza que el archivo tenga el tamaño exacto.
5. **Conecta al Kernel Memory** (`swap_conectar_kernel_memory` → `crear_conexion`).
6. **Handshake** con `enviar_handshake_swap(MEMORY_SOCKET, block_size, swap_file_size)` y loguea `## Conectado a Kernel Memory`.
7. Entra en `swap_atender_operaciones()` (loop bloqueante).

### Loop de atención ([utils_swap.c:123](src/utils_swap.c#L123))

```
mientras MEMORY_SOCKET sigue vivo:
  op = recibir_codigo_operacion()
  switch op:
    LECTURA_BLOQUE_SWAP  → swap_read_block()
    ESCRITURA_BLOQUE_SWAP → swap_write_block()
    default → warning, continúa
  fin
```

Si `recv` devuelve `-1` o falla cualquier handler, se cierra el socket y rompe el loop.

### IO sobre el archivo

Ambos handlers usan **I/O posicional** (`pread` / `pwrite`) para evitar manejar el cursor del archivo:

| Operación | Función | Cómo |
|---|---|---|
| Lectura | [`swap_read_block`](src/utils_swap.c#L53) | `pread(fd, buf, BLOCK_SIZE, block_num * BLOCK_SIZE)` |
| Escritura | [`swap_write_block`](src/utils_swap.c#L29) | `pwrite(fd, data, BLOCK_SIZE, block_num * BLOCK_SIZE)` |

Cada operación es atómica respecto al cursor (no hay race entre seeks).

### Validación de número de bloque

[`is_valid_block`](src/utils_swap.c#L23): rechaza pedidos cuyo `block_number >= SWAP_FILE_SIZE / BLOCK_SIZE`. Si falla, se responde al Kernel Memory con `status = -1` en lugar de los datos.

### Respuestas al Kernel Memory

| Operación | Wire de respuesta |
|---|---|
| Lectura | `enviar_bloque_swap(socket, status, data, BLOCK_SIZE)` |
| Escritura | `enviar_resultado_swap(socket, status)` |

`status = 0` éxito, `status = -1` error.

### Cierre controlado

`SIGINT` → `sigint_handler` ([swap_config.c:11](src/swap_config.c#L11)):
- Libera logger, config, cierra socket y FD del archivo.
- Si llega un segundo SIGINT antes de terminar, llama a `abort()` directo.

---

## Configuración

Archivo: [`swap.conf`](swap.conf)

| Campo | Tipo | Descripción |
|---|---|---|
| `LOG_LEVEL` | string | Compatible con `log_level_from_string()` |
| `MEMORY_IP` | string | IP del Kernel Memory |
| `MEMORY_PORT` | string | Puerto del Kernel Memory |
| `SWAP_FILE_PATH` | string | Path absoluto al archivo de SWAP (se crea si no existe) |
| `SWAP_FILE_SIZE` | uint32 | Tamaño total del archivo en bytes |
| `BLOCK_SIZE` | uint32 | Tamaño de cada bloque en bytes (`SWAP_FILE_SIZE % BLOCK_SIZE` no es validado pero conviene que divida exacto) |

`load_swap_config` valida que `BLOCK_SIZE > 0` y `BLOCK_SIZE <= SWAP_FILE_SIZE`.

---

## Uso

```bash
make
./bin/swap swap.conf
```

El archivo SWAP arranca vacío (relleno de ceros por `ftruncate`). El enunciado garantiza que no hay que limpiarlo entre ejecuciones — el Kernel Memory es el que sabe qué bloques están "libres" u "ocupados".

Para terminar el proceso usar `Ctrl+C` (libera todo controladamente).

---

## Logs obligatorios (formato exacto del enunciado)

| Evento | Formato |
|---|---|
| Conexión a KM | `## Conectado a Kernel Memory` |
| Escritura | `## Escritura del bloque: <NUMERO_BLOQUE>` |
| Lectura | `## Lectura del bloque: <NUMERO_BLOQUE>` |

Salida en consola + archivo `swap.log` (creado con `log_create(..., true, ...)`).
