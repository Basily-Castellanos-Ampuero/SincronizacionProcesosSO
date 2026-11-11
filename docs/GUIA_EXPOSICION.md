# Guía para la exposición

Documento para que todo el equipo entienda el trabajo y pueda explicarlo.
Leer primero el `README.md` (qué hace y cómo se ejecuta) y luego esta guía.

---

## 1. Idea en una frase

> Varios procesos (autos) quieren usar recursos limitados (espacios y una
> barrera). Sin control se pisan entre ellos; con **semáforos** cada uno espera
> su turno y nunca dos usan el mismo recurso a la vez.

## 2. Conceptos teóricos (lo que hay que saber explicar)

### Proceso
Programa en ejecución con su propio PID y su propia memoria. Con `fork()` el
padre crea un hijo que es una **copia**: después del `fork` cada uno tiene su
memoria separada. Por eso, para compartir datos, usamos `mmap` con `MAP_SHARED`.

### Recurso compartido
Algo que varios procesos usan: aquí los **espacios** del estacionamiento, la
**barrera** y las **variables** `plaza[]` y `ocupados`.

### Condición de carrera (race condition)
Ocurre cuando el resultado depende del **orden** en que los procesos se ejecutan.
Ejemplo de nuestro programa sin sincronización:

```
Auto A: lee ocupados = 2      (hay espacio, 2 < 3)
Auto B: lee ocupados = 2      (también ve espacio)
Auto A: escribe ocupados = 3  y entra
Auto B: escribe ocupados = 3  y entra   → hay 4 autos dentro, el contador dice 3
```

Se perdió una actualización (*lost update*) y se superó la capacidad.
`ocupados++` **no es una sola operación**: es *leer → sumar → escribir*, y otro
proceso puede meterse en medio.

### Sección crítica
Parte del código que accede a un recurso compartido y que **no debe ser ejecutada
por dos procesos a la vez**. En nuestro programa: buscar una plaza libre,
ocuparla y actualizar los contadores.

### Exclusión mutua
Garantía de que solo **un** proceso está dentro de la sección crítica. La
logramos con un semáforo binario (valor inicial 1).

Requisitos de una buena solución (los pregunta el profesor):
1. **Exclusión mutua**: solo uno dentro de la sección crítica.
2. **Progreso**: si nadie está dentro, alguien que quiere entrar puede hacerlo.
3. **Espera limitada**: ningún proceso espera para siempre.

### Semáforo
Variable entera protegida por el sistema operativo con dos operaciones atómicas:

| Operación | En C | Qué hace |
|---|---|---|
| **P** / wait / down | `sem_wait(&s)` | Si `s > 0`: `s--` y sigue. Si `s == 0`: el proceso se **bloquea** (duerme, no gasta CPU). |
| **V** / signal / up | `sem_post(&s)` | `s++` y despierta a un proceso bloqueado si lo hay. |

- **Semáforo binario** (valores 0/1): funciona como candado (mutex). Lo usamos
  para `barrera` y `mutex_estado`.
- **Semáforo contador** (valores 0..N): cuenta recursos disponibles. Lo usamos
  para `espacios` (inicia en 3 = tres espacios libres).

### Interbloqueo (deadlock)
Dos o más procesos se esperan mutuamente para siempre. Ejemplo: A tiene la barrera
y espera el mutex; B tiene el mutex y espera la barrera.

**¿Por qué no ocurre en nuestro programa?** Porque todos los procesos piden los
semáforos **en el mismo orden**: `espacios` → `barrera` → `mutex_estado`, y los
sueltan antes de volver a pedir. Al no haber espera circular, no hay deadlock.

### Inanición (starvation)
Un proceso espera indefinidamente porque otros siempre le ganan. Aquí no ocurre
en la práctica porque cada auto se va después de un tiempo finito y siempre libera
el espacio con `sem_post`. (Nota honesta: Linux no garantiza un orden estrictamente
FIFO al despertar procesos, por eso a veces entra un auto que llegó después.)

### Espera activa vs. bloqueo
- En `sin_sincronizacion.c` el auto que encuentra lleno hace un bucle
  `while (ocupados >= espacios) usleep(...)`: **espera activa**, consume CPU
  y además no evita la carrera.
- En `estacionamiento.c` el auto hace `sem_wait`: el sistema operativo lo
  **bloquea** y lo despierta cuando hay espacio.

## 3. Concepto ↔ código

| Concepto | Dónde está |
|---|---|
| Memoria compartida entre procesos | `estacionamiento.c:162` (`mmap` con `MAP_SHARED`) |
| Creación de semáforos entre procesos (`pshared = 1`) | `estacionamiento.c:170-172` (`sem_init`) |
| Creación de procesos | `estacionamiento.c:185` (`fork()`) |
| Semáforo contador: esperar espacio | `estacionamiento.c:67-70` |
| Exclusión mutua de la barrera | `estacionamiento.c:75` (`sem_wait(barrera)`) |
| Sección crítica: ocupar plaza | `estacionamiento.c:81-101` |
| Salida y liberar plaza | `estacionamiento.c:109-119` |
| Devolver espacio, despertar a otro | `estacionamiento.c:122` (`sem_post(espacios)`) |
| El padre lee el estado para dibujar (también usa el mutex) | `estacionamiento.c:127-140` (`tomar_foto`) |
| Padre espera a los hijos (sin zombis) | `estacionamiento.c:203` (`waitpid`) |
| Liberar recursos | `estacionamiento.c:230-232` (`sem_destroy`) y `munmap` |
| Condición de carrera (leer-modificar-escribir) | `sin_sincronizacion.c:97-99` |
| Espera activa | `sin_sincronizacion.c:68` |

## 4. Guion de la demostración (≈ 5 min)

1. **Compilar** delante del profesor:
   ```bash
   make clean && make
   ```
2. **Mostrar el problema** primero:
   ```bash
   make run-sin
   ```
   Señalar en el panel:
   - `BARRERA` en rojo con varios autos: `▶ A2 entra ▶ A4 entra ... ¡4 autos a la vez!`
   - Casillas rojas `2 AUTOS`: dos autos en el mismo espacio (y eventos `CHOQUE`).
   - `OCUPACIÓN` con bloques rojos de más y `¡SOBRECUPO!` (por ejemplo `5/3 dentro`).
   - `CONTADOR` en rojo: `ocupados = -3  ✘ no coincide` (actualizaciones perdidas).
   - `SEMÁFOROS: ninguno`.
   - El resumen final con las marcas ✘.
   - Si en esa corrida no salió error, ejecutar otra vez y explicar por qué
     (la carrera depende del tiempo: eso es justamente lo peligroso).
3. **Mostrar la solución**:
   ```bash
   make run
   ```
   Señalar en el panel:
   - Las casillas: nunca más de 3 autos, nunca dos en la misma.
   - `ESPERANDO espacio [A7] [A8]...`: cuando está lleno, los autos quedan
     bloqueados en `sem_wait(espacios)`; la línea `SEMÁFOROS` muestra `espacios = 0`.
   - `ESPERANDO barrera [A4]`: ya reservó lugar pero otro auto está en la barrera.
   - `BARRERA` siempre con **un solo** auto (verde).
   - `CONTADOR ✔ coincide` todo el tiempo y resumen con todas las marcas ✔.
4. **Probar otra configuración** para demostrar que no es casualidad:
   ```bash
   ./estacionamiento 20 5
   ./estacionamiento 5 1     # con 1 espacio se ve una fila perfecta
   ```
5. `make comparar` ejecuta las dos versiones seguidas (pide Enter entre una y otra).
6. (Opcional) En otra terminal, mientras corre, mostrar que son procesos reales:
   ```bash
   ps -o pid,ppid,stat,cmd -C estacionamiento
   ```
   Los autos que esperan aparecen en estado `S` (dormidos, bloqueados en el
   semáforo), todos con el mismo PPID (el padre).

## 5. Reparto sugerido (4 integrantes)

| Integrante | Parte | Contenido |
|---|---|---|
| 1 | Introducción y problema | Qué es sincronización, escenario del estacionamiento, procesos que intervienen. |
| 2 | Condición de carrera | Explicar y ejecutar `sin_sincronizacion`, sección crítica, por qué `ocupados++` falla. |
| 3 | Solución con semáforos | Semáforo binario vs contador, `sem_wait`/`sem_post`, memoria compartida, recorrer el código de `auto_proceso`. |
| 4 | Demo y conclusiones | Ejecutar `estacionamiento`, interpretar la salida, deadlock/inanición, conclusiones. |

(Si son 3, juntar las partes 1 y 2.)

## 6. Preguntas probables del profesor

**¿Por qué tres semáforos y no uno solo?**
Cada uno protege algo distinto. `espacios` cuenta recursos (puede haber 3 autos
dentro a la vez, eso no es exclusión mutua sino límite de capacidad). `barrera`
es exclusión mutua del recurso físico. `mutex_estado` protege las variables.
Con un solo mutex para todo, mientras un auto está estacionado nadie podría
entrar, y se perdería la concurrencia.

**¿Por qué `sem_init(..., 1, ...)` con un 1?**
El segundo argumento (`pshared`) indica que el semáforo se comparte entre
**procesos**. Con 0 solo serviría entre hilos del mismo proceso. Además el
semáforo tiene que estar en memoria compartida (`mmap MAP_SHARED`), si no cada
hijo tendría su propia copia y no se sincronizaría nada.

**¿Qué pasa si no se usa `mmap` y se declara una variable global?**
Después de `fork()` cada hijo tiene su copia de la variable (copy-on-write).
Los cambios de un auto no los vería ningún otro.

**¿Qué diferencia hay entre semáforo y mutex?**
Un mutex es un candado de dos estados que debe liberar quien lo tomó. Un semáforo
es un contador general; el binario se comporta como mutex. El contador permite
que hasta N procesos usen el recurso a la vez.

**¿Puede haber deadlock?**
No, porque todos piden los semáforos en el mismo orden (`espacios` → `barrera`
→ `mutex_estado`) y no hay espera circular. Si un auto pidiera `mutex_estado`
antes que `barrera` y otro al revés, sí podría ocurrir.

**¿Por qué se usa `sem_trywait` antes de `sem_wait`?**
Solo para poder imprimir "ESPERA" cuando el estacionamiento está lleno. La lógica
sería igual con solo `sem_wait`.

**¿Por qué el padre hace `wait()`?**
Para esperar a que todos los autos terminen antes de mostrar el resumen y
liberar los semáforos, y para que los hijos terminados no queden como
**procesos zombi**.

**¿Quién dibuja el panel? ¿No se mezcla la salida de 10 procesos?**
Los autos no imprimen nada: solo cambian su estado y anotan eventos en la
memoria compartida. El **padre** es el único que dibuja: cada 0,05 s toma una
"foto" del estado (dentro de `mutex_estado`, para no leer a mitad de un cambio)
y redibuja la pantalla. El código del panel está aparte, en `panel.c`, y no
forma parte de la sincronización del estacionamiento.

**¿Los tiempos son realistas?**
Sí, a escala 1:200 (`tiempos.h`): cruzar la barrera 10 s reales = 0,05 s, estar
estacionado de 10 min a 1 h = 3 a 18 s. El panel muestra también la hora real
equivalente. La única pausa artificial es la de `ocupados++` en la versión sin
sincronización, que existe solo para agrandar la ventana de la condición de carrera.

**¿Por qué a veces hay autos esperando y casillas libres?**
Porque el semáforo `espacios` cuenta **reservas**, no casillas pintadas: un auto
que espera la barrera ya reservó su lugar, y uno que está saliendo todavía no lo
devolvió. La línea `SEMÁFOROS` del panel muestra el valor real del contador.

**¿Por qué la versión sin sincronización a veces no falla?**
Porque la condición de carrera depende del momento exacto en que el planificador
ejecuta cada proceso. Que a veces funcione es lo que hace estos errores tan
difíciles de detectar; por eso hay que sincronizar siempre.

**¿Qué otros mecanismos existen?**
Mutex y variables de condición (`pthread_mutex`, `pthread_cond`), monitores,
paso de mensajes (tuberías, colas de mensajes), semáforos System V
(`semget`/`semop`), y soluciones por software como el algoritmo de Peterson.

**¿Qué problema clásico se parece a este?**
Es una variante del problema de **recursos limitados** / productor-consumidor
(el semáforo contador funciona como el número de huecos de un buffer) combinado
con exclusión mutua.

## 7. Conclusiones para cerrar

- Cuando varios procesos comparten datos, operaciones simples como `x++` dejan
  de ser seguras.
- Los semáforos permiten tanto **exclusión mutua** (binario) como **control de
  capacidad** (contador).
- El orden en que se toman los semáforos importa para evitar deadlock.
- Bloquear con semáforos es mejor que la espera activa: no gasta CPU.
