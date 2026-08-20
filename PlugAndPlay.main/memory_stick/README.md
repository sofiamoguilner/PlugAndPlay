# Memory Stick

Módulo del TP **Plug & Pray** que simula un chip de memoria RAM. Cumple el rol descripto en el enunciado (págs. 24-25): un proceso real del SO que reserva con `malloc()` un bloque de memoria contigua y atiende lecturas/escrituras pedidas por el Kernel Memory y las CPUs.

A lo largo de una ejecución pueden levantarse varios Memory Sticks; el Kernel Memory los enlaza al final de su lista y la memoria total del sistema crece dinámicamente.

---

## Qué hace

1. **Reserva memoria física simulada.** Al iniciar reserva con `calloc()` un buffer de `<tamaño>` bytes que va a oficiar de espacio físico de direcciones, con direcciones que arrancan en `0` (locales al stick).
2. **Se anuncia al Kernel Memory.** Le manda un handshake con su tamaño + IP/puerto propio para que el Kernel pueda redirigir CPUs hacia él.
3. **Escucha conexiones de CPUs.** Levanta un servidor multihilo. Cada CPU que se conecta abre una sesión propia y le manda pedidos de lectura/escritura sobre el buffer.
4. **Atiende lecturas y escrituras.** Recibe `(dirección física, tamaño)` —y para escrituras también `(contenido)`— y opera sobre el buffer reservado.
5. **Aplica `MEMORY_DELAY`.** Antes de contestar cada operación, hace `usleep` el tiempo configurado para simular latencia de hardware.

---

## Cómo lo hace

### Estructura del módulo

```
src/
├── main.c                       # Bootstrapping
├── memory_stick_config.{c,h}    # Carga del .conf
├── memory_stick_conexiones.{c,h}# Wrapper de inicialización
└── utils_memory_stick.{c,h}     # Buffer + servidor + handlers
```

### Flujo de arranque ([main.c](src/main.c))

1. Lee el config (`load_config_memory_stick`).
2. Si recibe un segundo argumento por CLI, lo usa como tamaño (sobreescribe `MEMORY_SIZE` del config).
3. Crea el logger.
4. `calloc(memory_size, 1)` → buffer físico inicializado en cero.
5. `inicializar_memoria_stick(buf, size)` → publica el buffer al módulo de utils mediante variables `static` protegidas con mutex.
6. **Levanta primero el servidor** y *después* notifica a Kernel Memory. El orden importa: cuando KM recibe el handshake, inmediatamente conecta de vuelta (y otras CPUs también van a querer conectarse vía KM).
7. `conectar_a_kernel_memory(...)` → handshake con `(tamaño, ip_propia, puerto_propio)`.
8. Loop `sleep(1)` para mantener vivo el proceso (todo el trabajo real lo hacen los threads).

### Threading model ([utils_memory_stick.c](src/utils_memory_stick.c))

- **Thread listener** (`memory_stick_listener_thread`): bloqueado en `accept`. Por cada cliente que llega:
  - Crea un thread *detached* `memory_stick_cpu_handler` y le pasa el socket.
- **Thread por CPU** (`memory_stick_cpu_handler`):
  - Valida que el primer mensaje sea `HANDSHAKE_CPU` y loguea `## CPU <ID> Conectada`.
  - Loopea recibiendo opcodes: `LECTURA_MEMORIA` o `ESCRITURA_MEMORIA`.
  - Cualquier otro opcode → asume desconexión y cierra la sesión.

### Buffer y concurrencia

El buffer es una variable `static uint8_t *memoria_buffer` protegida por `memoria_lock` (un `pthread_mutex_t`). Todas las lecturas y escrituras toman el mutex, así múltiples CPUs pueden coexistir sin races.

### Handlers de operaciones

| Operación | Handler | Wire format (recv) | Respuesta |
|---|---|---|---|
| Lectura | [`manejar_lectura`](src/utils_memory_stick.c#L32) | `uint32_t dir`, `uint32_t tam` | Paquete `LECTURA_MEMORIA` con `tam` bytes |
| Escritura | [`manejar_escritura`](src/utils_memory_stick.c#L58) | `uint32_t dir`, `uint32_t tam`, `tam` bytes | `enviar_ok(ESCRITURA_MEMORIA, ...)` |

Ambos handlers validan `dir + tam <= memoria_size`. Si no, loguean warning y no tocan el buffer (en lectura igual responden con paquete vacío para no colgar al cliente).

### Conexión a Kernel Memory ([`conectar_a_kernel_memory`](src/utils_memory_stick.c#L89))

Mensajes intercambiados:

```
Memory Stick ──[ HANDSHAKE_MEMORY_STICK + tamaño + ip + puerto ]──▶ Kernel Memory
```

El Kernel Memory responde internamente y luego notifica a las CPUs que existe un stick nuevo (`NUEVA_MEMORIA_DISPONIBLE`), por lo que las CPUs se conectarán por su cuenta al servidor del stick.

---

## Configuración

Archivo: [`configs/memory_stick.conf`](configs/memory_stick.conf)

| Campo | Tipo | Descripción |
|---|---|---|
| `LOG_LEVEL` | string | Compatible con `log_level_from_string()` |
| `MEMORY_IP` | string | IP del Kernel Memory |
| `MEMORY_PORT` | string | Puerto del Kernel Memory |
| `MEMORY_SIZE` | uint32 | Tamaño del buffer en bytes (el CLI puede sobreescribirlo) |
| `MEMORY_DELAY` | uint32 | Milisegundos a esperar antes de responder cada op |
| `STICK_IP` | string | IP propia (la que se le anuncia a KM para que las CPUs conecten) |
| `STICK_PORT` | string | Puerto donde escucha el servidor del stick |

Hay configs adicionales (`memory_stick_1.conf`, etc.) para levantar varios sticks en paralelo desde una misma máquina.

---

## Uso

```bash
make
./bin/memory_stick configs/memory_stick.conf            # usa MEMORY_SIZE del config
./bin/memory_stick configs/memory_stick.conf 512        # sobreescribe a 512 bytes
```

Para arrancar varios sticks en paralelo usar configs distintos (cada uno con su `STICK_PORT`):

```bash
./bin/memory_stick configs/memory_stick_1.conf &
./bin/memory_stick configs/memory_stick_2.conf &
./bin/memory_stick configs/memory_stick_3.conf &
```

---

## Logs obligatorios (formato exacto del enunciado)

| Evento | Formato |
|---|---|
| Conexión a KM | `## Conectado a Kernel Memory` |
| Conexión de CPU | `## CPU <ID CPU> Conectada` |
| Escritura | `## Escritura de <CANTIDAD ESCRITA> bytes` |
| Lectura | `## Lectura de <CANTIDAD ESCRITA> bytes` |

Salida en consola + archivo `memory_stick.log` (creado con `log_create(..., true, ...)`).
