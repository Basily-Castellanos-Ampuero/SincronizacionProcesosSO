# Sincronización de Procesos: Simulación de un Estacionamiento

Trabajo del curso de Sistemas Operativos. Se simula un estacionamiento con
**espacios limitados** y **una sola barrera** de entrada/salida. Cada auto es
un **proceso real** (creado con `fork()`) y todos compiten por los mismos
recursos. Se usan **semáforos POSIX** en **memoria compartida** para que dos
procesos nunca usen el mismo recurso al mismo tiempo.

Se incluyen dos programas:

| Programa | Qué demuestra |
|---|---|
| `sin_sincronizacion` | El problema: sin control, entran más autos que espacios, dos autos ocupan el mismo espacio y el contador queda mal (condición de carrera). |
| `estacionamiento` | La solución: con semáforos nunca se supera la capacidad, cada espacio lo usa un solo auto y el contador siempre es correcto. |

---

## 1. Escenario

- El estacionamiento tiene **M espacios** (por defecto 3).
- Llegan **N autos** (por defecto 10) en momentos distintos.
- La barrera de entrada/salida es **una sola**: solo un auto puede cruzarla a la vez.
- Si el estacionamiento está lleno, el auto **espera** (bloqueado) hasta que otro salga.
- Cada auto se queda estacionado un tiempo aleatorio (de 10 minutos a 1 hora,
  en tiempo real) y luego se va.

### Tiempos y escala

La simulación corre a **escala 1:200**: 1 segundo de simulación equivale a 200
segundos reales (3 min 20 s). Todos los tiempos están en `tiempos.h`, escritos en
segundos reales, y la macro `SEG()` los convierte:

| Acción | Tiempo real | En la simulación |
|---|---|---|
| Cruzar la barrera (entrada o salida) | 10 s | 0,05 s |
| Buscar el espacio y estacionar | 30 s | 0,15 s |
| Tiempo estacionado | 10 min a 1 h | 3 a 18 s |
| Llegadas, versión con semáforos (tráfico normal) | un auto cada ~1,5 min | uno cada ~0,45 s |
| Llegadas, versión sin sincronización (hora punta) | todos en 2 min | todos en 0,6 s |

Para que la demo vaya más lenta o más rápida basta con cambiar `ESCALA` en
`tiempos.h`. El panel muestra el tiempo simulado y el **tiempo real** equivalente
(h:mm:ss); los eventos también se muestran con la hora real.

## 2. Procesos que intervienen

```
                 ┌───────────────────────────────┐
                 │  PADRE (administrador)        │
                 │  - crea memoria compartida    │
                 │  - inicializa los semáforos   │
                 │  - lanza N hijos con fork()   │
                 │  - dibuja el panel en vivo    │
                 │  - espera a todos (waitpid)   │
                 │  - imprime el resumen         │
                 └──────────────┬────────────────┘
          fork()     fork()     │     fork()          fork()
     ┌──────────┬──────────┬────┴─────┬──────────┐
     ▼          ▼          ▼          ▼          ▼
  Auto 1     Auto 2     Auto 3      ...       Auto N      (procesos hijo)
```

- **Padre**: no conduce ningún auto; prepara los recursos, crea los hijos,
  dibuja el panel mientras corren, los espera (evitando procesos zombi) y
  verifica el resultado.
- **Hijos (autos)**: cada uno ejecuta la función `auto_proceso(id)` y termina.

Cuando un auto llega, el evento muestra su **PID**, así se comprueba que son
procesos distintos.

## 3. Recursos compartidos y sincronización

Los procesos no comparten memoria por defecto (después de `fork()` cada uno tiene
su copia). Por eso se crea una región con:

```c
mmap(NULL, tamaño, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
```

`MAP_SHARED` hace que los hijos vean **la misma** memoria que el padre. Ahí vive
la estructura `Estacionamiento`:

| Campo | Tipo | Valor inicial | Para qué sirve |
|---|---|---|---|
| `espacios` | semáforo **contador** | M | Cuenta los espacios libres. Si llega a 0, el auto que hace `sem_wait` se bloquea. |
| `barrera` | semáforo **binario** | 1 | Exclusión mutua: un solo auto cruza la barrera a la vez. |
| `mutex_estado` | semáforo **binario** | 1 | Protege los datos compartidos (`plaza[]`, `ocupados`, `max_ocupados`, `atendidos`). |
| `plaza[]` | arreglo de enteros | 0 | Qué auto está en cada espacio (0 = libre). |
| `ocupados` | entero | 0 | Cuántos autos hay dentro. |

Los semáforos se crean con `sem_init(&sem, 1, valor)`. El `1` (pshared) indica
que el semáforo se comparte **entre procesos** y no solo entre hilos.

Operaciones usadas:

- `sem_wait(&s)` (operación **P** / *wait*): si el valor es > 0 lo decrementa y
  sigue; si es 0, el proceso se **bloquea** hasta que alguien haga `sem_post`.
- `sem_post(&s)` (operación **V** / *signal*): incrementa el valor y despierta a
  un proceso bloqueado, si lo hay.
- `sem_trywait(&s)`: como `sem_wait` pero sin bloquear; se usa solo para poder
  imprimir "ESPERA" cuando el estacionamiento está lleno.

## 4. Flujo de cada auto (versión con semáforos)

```
   LLEGA
     │
     ▼
 sem_wait(espacios) ──── ¿hay espacio? ── no ──► queda BLOQUEADO (ESPERA)
     │ sí                                             │ (otro auto hace sem_post)
     ◄────────────────────────────────────────────────┘
     ▼
 sem_wait(barrera)          ┐
   cruza barrera ENTRADA    │
   sem_wait(mutex_estado)   │  SECCIÓN CRÍTICA
     busca plaza libre      │  (un solo auto a la vez)
     plaza[i] = id          │
     ocupados++             │
   sem_post(mutex_estado)   │
 sem_post(barrera)          ┘
     │
     ▼
 ESTACIONADO (10 min a 1 h) ← fuera de toda sección crítica, varios autos a la vez
     │
     ▼
 sem_wait(barrera)          ┐
   sem_wait(mutex_estado)   │
     plaza[i] = 0           │  SECCIÓN CRÍTICA
     ocupados--             │
   sem_post(mutex_estado)   │
   cruza barrera SALIDA     │
 sem_post(barrera)          ┘
     │
     ▼
 sem_post(espacios)         ← libera el espacio y despierta a un auto que espera
     │
     ▼
   TERMINA (exit)
```

**Orden fijo de los semáforos** (`espacios` → `barrera` → `mutex_estado`) en
todos los procesos: esto evita el **interbloqueo (deadlock)**.

## 5. Compilación y ejecución

Requisitos: Linux (o WSL), `gcc` y `make`.

```bash
make                 # compila los dos programas
make run-sin         # ejecuta la versión SIN sincronización (muestra el error)
make run             # ejecuta la versión CON semáforos (todo correcto)
make comparar        # ejecuta las dos seguidas (pide Enter entre una y otra)
make clean           # borra los ejecutables
```

Con parámetros propios:

```bash
./estacionamiento [autos] [espacios]       # autos: 1-100, espacios: 1-20
./estacionamiento 20 5
./sin_sincronizacion 10 3
```

## 6. Cómo leer la salida

Al ejecutarse en una terminal, el programa muestra un **panel que se redibuja en
vivo** (el padre lo actualiza cada 0,05 s). Ejemplo de la versión con semáforos:

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  ESTACIONAMIENTO · CON SEMÁFOROS    simulación  21.7 s  ·  tiempo real 1:12:16
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  LLEGANDO    —
  ESPERANDO   espacio  [A7] [A8] [A9] [A10]
              barrera  —

  BARRERA     ━━━━━━━━  libre

              ┌───────┬───────┬───────┐
  ESPACIOS    │  A4   │  A6   │  A5   │
              │  #1   │  #2   │  #3   │
              └───────┴───────┴───────┘

  OCUPACIÓN   ██ ██ ██  3/3 dentro
  CONTADOR    ocupados = 3   ✔ coincide
  SEMÁFOROS   espacios = 0   barrera = 1   (espacios en 0: el que llega espera)
  ATENDIDOS   3 de 10

  ── Últimos eventos ─────────────────────────────────────────────
  0:57:44  A1 deja el espacio #1 y sale
  0:57:54  A5 cruza la barrera de entrada
    ...
```

| Zona | Qué muestra |
|---|---|
| `LLEGANDO` | Autos (procesos) que todavía no llegan. |
| `ESPERANDO espacio` | Autos bloqueados en `sem_wait(espacios)` porque está lleno (amarillo). |
| `ESPERANDO barrera` | Autos que ya reservaron lugar y esperan que la barrera se libere. |
| `BARRERA` | Quién la está cruzando: `▶` entra, `◀` sale. En verde si es uno solo, en rojo si son varios. |
| `ESPACIOS` | Una casilla por espacio: `A4` = auto 4, `libre`, o `2 AUTOS` en rojo si hay choque. |
| `OCUPACIÓN` | Un bloque `██` por auto dentro; bloques rojos y `¡SOBRECUPO!` si se supera la capacidad. |
| `CONTADOR` | Valor de la variable compartida `ocupados` y si coincide con los autos realmente dentro. |
| `SEMÁFOROS` | Valor actual de `espacios` y `barrera` (en la versión sin sincronización: ninguno). |
| `Últimos eventos` | Las 8 acciones más recientes, con color según el tipo. |

Al terminar se imprime un **resumen** con ✔ / ✘:

```
   RESUMEN
  ✔ Autos atendidos: 10 de 10
  ✔ Máximo dentro a la vez: 3 (capacidad 3)
  ✔ Contador al final: 0
  ✔ Nunca se superó la capacidad ni se compartió un espacio
```

En la versión **sin sincronización** se ven los errores en el mismo panel: la
barrera en rojo con varios autos a la vez, casillas `2 AUTOS`, `¡SOBRECUPO!` y el
contador que no coincide. El resumen termina, por ejemplo, así:

```
  ✔ Autos atendidos: 10 de 10
  ✘ Máximo dentro a la vez: 8 (capacidad 3) → se superó la capacidad
  ✘ Choques por un mismo espacio: 6
  ✘ Contador al final: -3 (debería ser 0) → actualizaciones perdidas
```

Los errores dependen del momento exacto en que corre cada proceso (**condición
de carrera**), así que los números cambian en cada ejecución; si en alguna no se
nota, basta con ejecutar otra vez.

**Modo texto:** si la salida no es una terminal (por ejemplo
`./estacionamiento | cat` o `> salida.txt`), en lugar del panel se imprime un
evento por línea, útil para copiar en un informe. `NO_COLOR=1` desactiva los colores.

> En `sin_sincronizacion.c` hay contadores atómicos (`dentro_real`, `max_real`,
> `choques`). Son solo un "medidor" para poder demostrar el error; no forman parte
> de la lógica del estacionamiento.

## 7. Estructura del proyecto

```
sincronizacionProcesos/
├── Makefile
├── README.md                 ← este documento
├── estacionamiento.c         ← solución con semáforos
├── sin_sincronizacion.c      ← versión con condición de carrera
├── panel.c / panel.h         ← dibujo del panel en la terminal (no sincroniza nada)
├── tiempos.h                 ← tiempos reales y escala de la simulación
└── docs/
    └── GUIA_EXPOSICION.md    ← teoría, guion de la demo y preguntas
```
