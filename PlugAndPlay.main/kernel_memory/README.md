# Kernel Memory

Módulo central de memoria del sistema operativo simulado. Funciona como
**servidor** al que se conectan el Kernel Scheduler, las CPUs, los Memory
Sticks (RAM física distribuida) y el módulo Swap. Gestiona procesos,
contextos de ejecución, traducción de direcciones lógicas a físicas,
asignación/liberación de segmentos, compactación, y suspensión/des-suspensión
contra Swap.

> Toda la RAM real del sistema vive en los **Memory Sticks**. Kernel Memory
> no aloca un buffer físico propio: solo administra el espacio agregado de los
> sticks y enruta cada lectura/escritura al stick correspondiente.

---

## Compilación y ejecución

```bash
cd kernel_memory
make
./bin/kernel_memory ./configs/kernel_memory.conf
# o equivalentemente
./run.sh
```

El binario queda en `bin/kernel_memory`. Los logs se escriben en
[kernel_memory.log](kernel_memory.log).

---

## Configuración (`configs/kernel_memory.conf`)

| Parámetro             | Tipo     | Uso real en el código                                                                                  |
|-----------------------|----------|--------------------------------------------------------------------------------------------------------|
| `LOG_LEVEL`           | string   | `TRACE` / `DEBUG` / `INFO` / `WARNING` / `ERROR`. Pasado a `log_level_from_string`                     |
| `TOTAL_MEMORY`        | uint32   | Cargado en config; **hoy no se usa** — el espacio total se calcula sumando los Memory Sticks conectados |
| `SEGMENT_MAX_SIZE`    | int      | Tamaño de página/segmento usado por `traducir_dir_logica` (`num_seg = dir_logica / SEGMENT_MAX_SIZE`)  |
| `ALLOCATION_STRATEGY` | string   | `BEST_FIT` (default) o `WORST_FIT` para `asignar_espacio_libre`                                        |
| `INSTRUCTION_DELAY`   | int (ms) | Delay artificial — cargado pero no aplicado actualmente                                                 |
| `COMPACTION_DELAY`    | int (ms) | Aplicado con `usleep` al final de `compactar_memoria`                                                   |
| `SCRIPTS_BASEPATH`    | string   | Directorio donde se buscan los scripts cuando `CREAR_PROCESO` recibe un path relativo                  |
| `PUERTO_ESCUCHA`      | string   | Puerto del servidor TCP                                                                                |

Definidos en [src/memory_config.h:9-18](src/memory_config.h#L9-L18) y cargados
en [src/memory_config.c:11-35](src/memory_config.c#L11-L35).

---

## Arquitectura general

```
                   ┌─────────────────────────────────────────┐
                   │         KERNEL MEMORY (servidor)        │
                   │                                         │
   Scheduler ────► │  • registro de procesos                 │
                   │  • tabla de bloques libres              │
   CPU 0 ───────► │  • compactación                          │
   CPU 1 ───────► │  • traducción dir_lógica → dir_física    │
                   │  • orquesta swap                        │
   Stick A ──────►│  ─── outbound ──► Stick A (RAM real)    │
   Stick B ──────►│  ─── outbound ──► Stick B (RAM real)    │
                   │                                         │
   Swap ─────────►│  ─── canal swap                          │
                   └─────────────────────────────────────────┘
```

Kernel Memory **acepta conexiones entrantes de todos los módulos** y, además,
**abre conexiones salientes hacia cada Memory Stick** para poder leer/escribir
RAM física. La conexión hacia el Swap es solo entrante: Swap se conecta a
Kernel Memory y queda esperando órdenes.

---

## Threads

| Thread                   | Función                                | Archivo                                                      | Lifecycle                                |
|--------------------------|----------------------------------------|--------------------------------------------------------------|------------------------------------------|
| Listener                 | `_memory_listener_thread`              | [memory_servidor.c:88](src/memory_servidor.c#L88)            | Se lanza en `iniciar_servidor_memory`    |
| Handshake handler (×N)   | `memory_handshake_handler`             | [memory_servidor.c:51](src/memory_servidor.c#L51)            | Uno por nueva conexión, identifica módulo|
| Receptor del Scheduler   | `memory_receptor_scheduler_thread`     | [memory_scheduler.c:43](src/memory_scheduler.c#L43)          | Uno solo, se lanza en `registrar_scheduler` |
| Receptor de CPU (×N)     | `memory_receptor_cpu_thread`           | [memory_cpu.c:82](src/memory_cpu.c#L82)                      | Uno por CPU conectada                    |
| Monitor de Memory Stick  | `memory_stick_monitor_thread`          | [memory_cpu.c:14](src/memory_cpu.c#L14)                      | Detecta desconexión vía `recv MSG_PEEK`  |

Todos se lanzan vía `spawn_detached` (`pthread_create + pthread_detach`) en
[memory_servidor.c:20](src/memory_servidor.c#L20).

---

## Estructuras de datos

### `t_contexto_ejecucion` — proceso en memoria
Definido en [memory_tipos.h:16-22](src/memory_tipos.h#L16-L22):

```c
typedef struct {
    int         pid;
    t_registros registros;             // estado de CPU
    t_list*     segmentos;             // lista de t_segmento (id/base/limite/swap_bloque_inicio)
    char**      instrucciones;         // script cargado en memoria
    int         cantidad_instrucciones;
} t_contexto_ejecucion;
```

### `t_memory_stick_entry` — un stick conectado
Definido en [memory_tipos.h:48-56](src/memory_tipos.h#L48-L56):

```c
typedef struct {
    int             socket;           // socket entrante (handshake)
    uint32_t        tamanio;
    char*           ip;
    char*           puerto;
    int             outbound_socket;  // socket SALIENTE para hacer I/O
    pthread_mutex_t outbound_lock;    // serializa I/O sobre outbound_socket
    uint32_t        base_fisica;      // offset cumulativo en el espacio físico global
} t_memory_stick_entry;
```

`base_fisica` es la clave del esquema: cada stick contribuye `tamanio` bytes
al espacio físico global, ubicados a partir de `base_fisica`. Cuando hay que
acceder a una `dir_fisica` global, Kernel Memory busca el stick `s` tal que
`s.base_fisica ≤ dir_fisica < s.base_fisica + s.tamanio` y traduce a un
offset local.

### `t_conexiones_activas` — tabla de conexiones (`active`)
Definida en [memory_tipos.h:58-66](src/memory_tipos.h#L58-L66) y declarada
global en [memory_procesos.c:29](src/memory_procesos.c#L29). Toda lectura o
modificación se hace bajo `active.lock`.

### Estado global de memoria
Declarado en [memory_procesos.h](src/memory_procesos.h):

| Variable                | Descripción                                                                |
|-------------------------|----------------------------------------------------------------------------|
| `procesos`              | `t_list*` de `t_contexto_ejecucion*` (todos los procesos en memoria)       |
| `bloques_libres`        | `t_list*` de `t_bloque_libre*` (huecos libres en el espacio físico global) |
| `mutex_memoria`         | Protege `procesos` y `bloques_libres`                                      |
| `mutex_swap`            | Protege `swap_next_block`                                                  |
| `swap_block_size`       | Tamaño de bloque del módulo Swap (fijado en su handshake)                  |
| `swap_total_blocks`     | Bloques totales en swap                                                    |
| `swap_next_block`       | Asignador bump: próximo bloque libre                                        |
| `compactacion_en_progreso` | `atomic_int` — bloquea `LECTURA_MEMORIA`/`ESCRITURA_MEMORIA` durante compactación |

---

## Conexión y handshake

Kernel Memory escucha en `PUERTO_ESCUCHA` y para cada conexión entrante
ejecuta `memory_handshake_handler`, que recibe el primer opcode y lo
interpreta como un identificador de módulo:

| Opcode entrante                  | Módulo           | Handler de registro                              |
|----------------------------------|------------------|--------------------------------------------------|
| `HANDSHAKE_SCHEDULER_A_MEMORY`   | Kernel Scheduler | `registrar_scheduler` ([memory_scheduler.c:25](src/memory_scheduler.c#L25)) |
| `HANDSHAKE_CPU`                  | CPU              | `registrar_cpu` ([memory_cpu.c:53](src/memory_cpu.c#L53)) |
| `HANDSHAKE_MEMORY_STICK`         | Memory Stick     | `registrar_memory_stick` ([memory_cpu.c:216](src/memory_cpu.c#L216)) |
| `HANDSHAKE_SWAP`                 | Swap             | `registrar_swap` ([memory_servidor.c:33](src/memory_servidor.c#L33)) |

### Registro del Scheduler
1. Se hace un intercambio `recibir_ok` / `enviar_ok`.
2. El socket queda guardado en `active.scheduler_socket`.
3. Se lanza `memory_receptor_scheduler_thread`.

### Registro de una CPU
1. Se recibe `cpu_id` con `recibir_handshake_cpu`.
2. Se inserta en `active.cpus[]`.
3. **Se le notifican todos los Memory Sticks ya conectados** con
   `NUEVA_MEMORIA_DISPONIBLE` (la CPU necesita conocer cada stick para
   poder leerle/escribirle directamente).
4. Se lanza `memory_receptor_cpu_thread`.

### Registro de un Memory Stick
1. Se recibe `tamanio`, `ip`, `puerto` (`recibir_handshake_memory_stick`).
2. Se calcula `base_fisica` como suma de tamaños de los sticks ya registrados.
3. Se inserta en `active.sticks[]`.
4. Se notifica a todas las CPUs conectadas con `NUEVA_MEMORIA_DISPONIBLE`.
5. Se agrega un nuevo `t_bloque_libre` `{ base_fisica, tamanio }` a
   `bloques_libres`.
6. **Se abre una conexión saliente** hacia `ip:puerto` (Kernel Memory actúa
   como CPU con `cpu_id = -999` ante el stick) y se guarda en
   `outbound_socket`. Esta conexión es la que se usa para todo el I/O
   posterior.
7. Si el Scheduler está conectado, se le notifica la nueva memoria.
8. Se lanza `memory_stick_monitor_thread` para detectar desconexión.

### Registro del Swap
1. Se recibe `block_size` y `total_size` (`recibir_handshake_swap`).
2. Se calculan `swap_block_size` y `swap_total_blocks = total_size / block_size`.
3. Se guarda el socket en `active.swap_socket`.

---

## Operaciones que ofrece a la CPU

Una vez conectada, una CPU dialoga con su `memory_receptor_cpu_thread`:

### `OBTENER_INSTRUCCION` — fetch de instrucción
[memory_cpu.c:129](src/memory_cpu.c#L129)

```
CPU → [ OBTENER_INSTRUCCION | pid : int | pc : uint32 ]
Memory → [ OBTENER_INSTRUCCION | string ]
```

Busca el proceso, devuelve `instrucciones[pc]`. Si el `pc` está fuera de
rango, loguea error y devuelve string vacío. **No hay delay artificial
aplicado** a pesar de `INSTRUCTION_DELAY`.

### `OBTENER_CONTEXTO`
[memory_cpu.c:156](src/memory_cpu.c#L156)

```
CPU → [ OBTENER_CONTEXTO | pid : int ]
Memory → [ OBTENER_CONTEXTO | registros | cantidad_segmentos | seg[0] | ... ]
```

Si el PID no existe, **modo debug**: crea un proceso dummy cargando
`{SCRIPTS_BASEPATH}/{pid}.script` para permitir que la CPU pueda ejecutar
sin pasar por el Scheduler.

### `ACTUALIZAR_CONTEXTO`
[memory_cpu.c:193](src/memory_cpu.c#L193)

```
CPU → [ ACTUALIZAR_CONTEXTO | pid | registros | cantidad_seg | seg[0] | ... ]
Memory → [ ACTUALIZAR_CONTEXTO | OK ]
```

Reemplaza `proc->registros` y reconstruye `proc->segmentos` desde los datos
recibidos.

---

## Operaciones que ofrece al Scheduler

Despachadas por `memory_receptor_scheduler_thread`
([memory_scheduler.c:43](src/memory_scheduler.c#L43)).

### `CREAR_PROCESO`
[memory_scheduler.c:113](src/memory_scheduler.c#L113)

Recibe `pid` y `path`. Si `path` no es absoluto, lo prefija con
`SCRIPTS_BASEPATH`. Crea un `t_contexto_ejecucion` con `segmentos = []` y
las instrucciones cargadas vía `cargar_script`. Responde `OK`.

### `OBTENER_ESPACIO_LIBRE`
[memory_scheduler.c:169](src/memory_scheduler.c#L169)

Recorre `bloques_libres` y suma. Devuelve la **suma total**, no el bloque
contiguo más grande.

```
Scheduler → [ OBTENER_ESPACIO_LIBRE ]
Memory    → [ OBTENER_ESPACIO_LIBRE | uint32_t ]
```

### `LECTURA_MEMORIA`
[memory_scheduler.c:183](src/memory_scheduler.c#L183)

```
Scheduler → [ LECTURA_MEMORIA | pid | dir_logica | tamanio ]
Memory    → [ LECTURA_MEMORIA | datos ]   o   [ SEGMENTATION_FAULT ]
```

1. Espera a que termine cualquier compactación en curso (busy-wait sobre
   `compactacion_en_progreso`).
2. Traduce `dir_logica → dir_fisica` con `traducir_dir_logica`.
3. Llama `leer_de_memory_sticks(dir_fisica, tamanio)`, que puede **partir
   la lectura entre múltiples sticks** si la región cruza fronteras.
4. Si algún stick está caído, envía `MEMORIA_CORRUPTA` (BSOD) al Scheduler.

### `ESCRITURA_MEMORIA`
[memory_scheduler.c:220](src/memory_scheduler.c#L220)

Análogo a la lectura, usando `escribir_en_memory_sticks`. Responde `OK` o
`SEGMENTATION_FAULT`.

### `SOLICITAR_SEGMENTO` — crear segmento (con compactación)
[memory_scheduler.c:458](src/memory_scheduler.c#L458)

```
Scheduler → [ SOLICITAR_SEGMENTO | pid | id_segmento | tamanio ]
Memory    → [ SOLICITAR_SEGMENTO | id_segmento | base | tamanio ]
            [ SEGMENTATION_FAULT ]
```

Flujo:
1. `asignar_espacio_libre(tamanio)` con BEST_FIT/WORST_FIT.
2. Si **no hay bloque contiguo** suficiente pero **el espacio libre total
   sí alcanza** → dispara compactación:
   - Set `compactacion_en_progreso = 1`.
   - `INICIAR_COMPACTACION` → Scheduler (espera `OK` para que pause CPUs).
   - `compactar_memoria()`.
   - `FINALIZAR_COMPACTACION` → Scheduler.
   - Reintenta `asignar_espacio_libre`.
3. Si tampoco después de compactar → `SEGMENTATION_FAULT`.
4. Inserta el segmento en `proc->segmentos` y responde la base asignada.

### `ELIMINAR_SEGMENTO`
[memory_scheduler.c:549](src/memory_scheduler.c#L549)

Busca el segmento por `id`, llama `liberar_bloque(seg->base, seg->limite)`,
lo remueve de la lista. Responde `OK`.

### `FINALIZAR_PROCESO`
[memory_scheduler.c:580](src/memory_scheduler.c#L580)

Libera todos los segmentos del proceso (los que tengan `base != -1`),
destruye la lista de segmentos, libera las instrucciones, y remueve el
proceso de la lista global. Responde `OK`.

### `ACTUALIZAR_CONTEXTO` (desde Scheduler)
[memory_scheduler.c:150](src/memory_scheduler.c#L150)

Mismo formato que el de CPU, pero el `proc` ya debe existir.

### `SUSPENDER_PROCESO` — RAM → Swap
[memory_scheduler.c:260](src/memory_scheduler.c#L260)

Para cada segmento del proceso:
1. Calcula `n_blocks = ⌈limite / swap_block_size⌉`.
2. Reserva esos bloques en swap incrementando `swap_next_block` (asignador
   bump, sin reuso).
3. Lee cada chunk desde Memory Sticks → escribe el bloque en Swap.
4. **Libera la memoria física** del segmento (`liberar_bloque`).
5. Marca `seg->base = -1` (señal de "está en swap").

> El espacio en swap **no se libera** hasta `FINALIZAR_PROCESO` — los
> bloques quedan ocupados aunque después se haga des-suspender. Es un
> asignador bump puro.

### `DES_SUSPENDER_PROCESO` — Swap → RAM
[memory_scheduler.c:355](src/memory_scheduler.c#L355)

1. **Reserva nuevas bases en RAM para todos los segmentos** primero
   (asignar_espacio_libre por segmento). Si falla en algún segmento, hace
   **rollback** liberando las reservas previas y restaurando bases.
2. Lee de Swap bloque por bloque → escribe a Memory Sticks.

> Si la des-suspensión falla por falta de RAM, **no se dispara
> compactación** automática (a diferencia de `SOLICITAR_SEGMENTO`).

---

## Traducción de direcciones

[memory_procesos.c:154](src/memory_procesos.c#L154)

Esquema de **segmentación pura** sobre un espacio físico contiguo virtual:

```
num_seg        = dir_logica / SEGMENT_MAX_SIZE
desplazamiento = dir_logica % SEGMENT_MAX_SIZE
```

Se busca `seg` con `seg->id == num_seg` en la lista de segmentos del
proceso. Si no existe o `desplazamiento + tamanio > seg->limite`, se loguea
SEGMENTATION FAULT y se envía `SEGMENTATION_FAULT` al socket.

`dir_fisica = seg->base + desplazamiento`

Esa `dir_fisica` está en el **espacio agregado de los sticks**.
`leer_de_memory_sticks` / `escribir_en_memory_sticks` la resuelven a
`{stick, offset_local}` y dividen el acceso si cruza fronteras.

---

## Asignación de bloques libres

[memory_procesos.c:326](src/memory_procesos.c#L326)

`asignar_espacio_libre(tamanio)` recorre `bloques_libres`:

- **`BEST_FIT`** (default): elige el bloque más chico que satisface el pedido.
- **`WORST_FIT`**: elige el bloque más grande.

Si el bloque elegido es exacto se elimina; si es más grande, se hace
**split** (corre la base y reduce el tamaño). No hay FIRST_FIT.

`liberar_bloque(base, tamanio)` agrega un nuevo bloque libre y luego hace
**merge multi-pasada**: mientras haya dos bloques `a, b` con `a.base +
a.tamanio == b.base`, los fusiona. (No se ordena la lista, así que cada
pasada es O(N²); se itera hasta que un barrido completo no fusiona.)

---

## Compactación

[memory_procesos.c:430](src/memory_procesos.c#L430)

Disparada exclusivamente desde `SOLICITAR_SEGMENTO`.

1. Recolecta **todos los segmentos de todos los procesos** en un array.
2. Los ordena por `seg->base` ascendente (`qsort`).
3. Recorre con un cursor desde 0:
   - Si `seg->base != cursor`, el segmento se mueve:
     - `leer_de_memory_sticks(old_base, limite)` → buffer.
     - `escribir_en_memory_sticks(cursor, limite, buffer)`.
     - `seg->base = cursor`.
     - Loguea con el formato obligatorio:
       `## Compactación: Segmento N - PID X - Dir. Anterior: 0xY - Dir. Nueva: 0xZ`.
   - `cursor += limite`.
4. Reconstruye `bloques_libres` como **un único bloque** desde `cursor`
   hasta el final del espacio total (suma de tamaños de sticks).
5. `usleep(COMPACTION_DELAY * 1000)`.

Mientras está activa, `LECTURA_MEMORIA` y `ESCRITURA_MEMORIA` desde el
Scheduler quedan en busy-wait.

---

## Detección de desconexiones

### Memory Stick caído
`memory_stick_monitor_thread` ([memory_cpu.c:14](src/memory_cpu.c#L14)) hace
un `recv(MSG_PEEK)` que solo retorna cuando el otro extremo cierra la
conexión. Al detectar la caída:
- Setea `stick->socket = -1`.
- Loguea `## Memory Stick desconectada - memoria corrupta`.
- Envía `MEMORIA_CORRUPTA` al Scheduler.

> El stick **no se remueve** del array, solo se marca con `socket = -1`.
> Las funciones de I/O cross-stick chequean `outbound_socket >= 0`. Si
> durante una operación in-flight se rompe el outbound, también se envía
> `MEMORIA_CORRUPTA` al Scheduler.

### CPU caída
El `memory_receptor_cpu_thread` detecta el cierre por `recibir_codigo_operacion`
devolviendo `-1`, marca `socket = -1` en `active.cpus[]` y termina.

### Scheduler caído
Detectado igual: `scheduler_socket = -1` y se rompe el thread receptor.

---

## Concurrencia y locks

| Lock                          | Protege                                                   | Notas                                                                |
|-------------------------------|-----------------------------------------------------------|----------------------------------------------------------------------|
| `active.lock`                 | `cpus[]`, `sticks[]`, `scheduler_socket`, `swap_socket`   | Tomar siempre **antes** que un `outbound_lock`                       |
| `mutex_memoria`               | `procesos`, `bloques_libres`                              | Lo toman handlers de scheduler/CPU; las funciones I/O **no** lo toman |
| `mutex_swap`                  | `swap_next_block`                                         |                                                                      |
| `stick->outbound_lock`        | I/O sobre `outbound_socket` de un stick                   | Serializa request/response del par send/recv                         |
| `compactacion_en_progreso`    | atomic_int (no es un mutex)                               | Las lecturas/escrituras del Scheduler hacen busy-wait sobre él       |

Reglas clave (ver comentarios en `memory_procesos.h`):
- `traducir_dir_logica`, `asignar_espacio_libre`, `liberar_bloque`,
  `calcular_espacio_libre`, `compactar_memoria` → **caller debe tener
  `mutex_memoria`**.
- `leer_de_memory_sticks`, `escribir_en_memory_sticks` → **caller NO debe
  tener `mutex_memoria` ni `outbound_lock`** (toman `active.lock` y
  `outbound_lock` internamente).

---

## Mapa de archivos

| Archivo                                                | Responsabilidad                                                                                                          |
|--------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| [src/main.c](src/main.c)                               | `main`: carga config, inicia logger, inicia listas/mutexes, arranca el servidor, loop infinito de `sleep(1)`             |
| [src/memory_config.c](src/memory_config.c)             | Carga/destrucción de la config y del logger global                                                                       |
| [src/memory_tipos.h](src/memory_tipos.h)               | Structs compartidas (`t_contexto_ejecucion`, `t_bloque_libre`, `t_memory_stick_entry`, `t_conexiones_activas`)           |
| [src/memory_procesos.c](src/memory_procesos.c)         | Estado global; carga de scripts; serialización de contextos; traducción de direcciones; I/O cross-stick; bloques libres; compactación |
| [src/memory_servidor.c](src/memory_servidor.c)         | Listener TCP, dispatcher de handshake, registro de Swap, helper `spawn_detached`                                         |
| [src/memory_cpu.c](src/memory_cpu.c)                   | Registro de CPUs y Memory Sticks; receptor por CPU; handlers `OBTENER_INSTRUCCION` / `OBTENER_CONTEXTO` / `ACTUALIZAR_CONTEXTO`; monitor de sticks; notificaciones cruzadas CPU↔stick |
| [src/memory_scheduler.c](src/memory_scheduler.c)       | Registro del Scheduler; receptor; todos los handlers que el Scheduler dispara (CREAR/FINALIZAR_PROCESO, LECTURA/ESCRITURA, SOLICITAR/ELIMINAR_SEGMENTO, SUSPENDER/DES_SUSPENDER, etc.) |

---

## Formato de scripts de proceso

Cada proceso se identifica por su `pid`. Por convención su programa está en
`{SCRIPTS_BASEPATH}/{pid}.script` (cuando se usa el modo dummy de CPU) o en
el path indicado por el Scheduler en `CREAR_PROCESO`.

Una instrucción por línea, base 0. El `pc` es el número de línea solicitado
por la CPU.

```
SET AX 5
SET BX 3
SUM AX BX
EXIT
```
