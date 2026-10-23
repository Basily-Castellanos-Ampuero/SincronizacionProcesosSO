/*
 * Sincronizacion de procesos: el MISMO estacionamiento pero SIN semaforos.
 *
 * Sirve para demostrar la condicion de carrera. Cada auto:
 *   1. mira si hay espacio  (lee "ocupados")
 *   2. tarda un momento en cruzar la barrera
 *   3. elige un espacio libre y lo ocupa (escribe "ocupados")
 * Entre el paso 1 y el 3 otros autos pueden hacer lo mismo, porque nadie
 * les impide entrar a la vez. Resultado: entran mas autos que espacios,
 * dos autos terminan en el mismo espacio y el contador queda mal.
 *
 * Los campos "dentro_real", "max_real" y "choques" son un medidor externo
 * (operaciones atomicas) que solo sirve para comprobar el error; no forman
 * parte de la logica del estacionamiento.
 *
 * Uso: ./sin_sincronizacion [autos] [espacios]   (por defecto 10 autos y 3 espacios)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ESPACIOS 20
#define MAX_AUTOS    100

typedef struct {
    int num_espacios;
    volatile int plaza[MAX_ESPACIOS];   /* numero de auto en cada espacio, 0 = libre */
    volatile int ocupados;              /* contador compartido SIN proteccion */

    int dentro_real;                    /* medidor externo (atomico) */
    int max_real;
    int choques;
    int atendidos;
    struct timespec inicio;
} Estacionamiento;

static Estacionamiento *est;

static double segundos(void)
{
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (ahora.tv_sec - est->inicio.tv_sec) +
           (ahora.tv_nsec - est->inicio.tv_nsec) / 1e9;
}

static void registrar(const char *fmt, ...)
{
    char linea[512];
    int n = snprintf(linea, sizeof linea, "[t=%5.2fs] ", segundos());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(linea + n, sizeof linea - n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof linea - 2)
        n = sizeof linea - 2;
    linea[n++] = '\n';
    if (write(STDOUT_FILENO, linea, n) < 0)
        perror("write");
}

static void medir_entrada(void)
{
    int dentro = __atomic_add_fetch(&est->dentro_real, 1, __ATOMIC_SEQ_CST);
    int max = __atomic_load_n(&est->max_real, __ATOMIC_SEQ_CST);
    while (dentro > max &&
           !__atomic_compare_exchange_n(&est->max_real, &max, dentro, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        ;
}

static void auto_proceso(int id)
{
    srand(getpid() ^ time(NULL));
    usleep(rand() % 1500000);                 /* varios llegan casi juntos */

    registrar("Auto %2d (PID %d) LLEGA", id, getpid());

    /* 1. Mirar si hay espacio: lectura sin proteccion */
    if (est->ocupados >= est->num_espacios) {
        registrar("Auto %2d ESPERA: estacionamiento lleno", id);
        while (est->ocupados >= est->num_espacios)
            usleep(50000);                    /* espera activa */
    }

    /* 2. Cruzar la barrera: no hay control, pueden pasar varios a la vez */
    registrar("Auto %2d cruza la barrera de ENTRADA", id);
    usleep(200000);

    /* 3. Elegir un espacio que "parece" libre */
    int mi_plaza = 0;
    for (int i = 0; i < est->num_espacios; i++) {
        if (est->plaza[i] == 0) {
            mi_plaza = i;
            break;
        }
    }
    usleep(50000);                            /* tiempo de maniobra */
    int otro = est->plaza[mi_plaza];
    if (otro != 0) {
        __atomic_add_fetch(&est->choques, 1, __ATOMIC_SEQ_CST);
        registrar("Auto %2d CHOQUE: el espacio %d ya lo ocupa el auto %d",
                  id, mi_plaza + 1, otro);
    }
    est->plaza[mi_plaza] = id;

    /* ocupados++ en tres pasos (leer, modificar, escribir): se pierden
     * actualizaciones si otro proceso escribe en medio */
    int tmp = est->ocupados;
    usleep(10000);
    est->ocupados = tmp + 1;
    medir_entrada();

    registrar("Auto %2d ESTACIONA en espacio %d   contador=%d  autos dentro=%d/%d",
              id, mi_plaza + 1, est->ocupados,
              __atomic_load_n(&est->dentro_real, __ATOMIC_SEQ_CST),
              est->num_espacios);

    usleep(1000000 + rand() % 1000000);

    /* Salir: mismas operaciones sin proteccion */
    if (est->plaza[mi_plaza] == id)
        est->plaza[mi_plaza] = 0;
    tmp = est->ocupados;
    usleep(10000);
    est->ocupados = tmp - 1;
    __atomic_sub_fetch(&est->dentro_real, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&est->atendidos, 1, __ATOMIC_SEQ_CST);
    registrar("Auto %2d SALE del espacio %d       contador=%d", id, mi_plaza + 1,
              est->ocupados);
}

int main(int argc, char *argv[])
{
    int autos    = (argc > 1) ? atoi(argv[1]) : 10;
    int espacios = (argc > 2) ? atoi(argv[2]) : 3;

    if (autos < 1 || autos > MAX_AUTOS || espacios < 1 || espacios > MAX_ESPACIOS) {
        fprintf(stderr, "Uso: %s [autos 1-%d] [espacios 1-%d]\n",
                argv[0], MAX_AUTOS, MAX_ESPACIOS);
        return EXIT_FAILURE;
    }

    est = mmap(NULL, sizeof *est, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (est == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    est->num_espacios = espacios;
    clock_gettime(CLOCK_MONOTONIC, &est->inicio);

    printf("=== Estacionamiento SIN sincronizacion ===\n");
    printf("Administrador PID %d | %d autos | %d espacios\n\n",
           getpid(), autos, espacios);
    fflush(stdout);

    int creados = 0;
    for (int i = 1; i <= autos; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            break;
        }
        if (pid == 0) {                              /* HIJO */
            auto_proceso(i);
            exit(EXIT_SUCCESS);
        }
        creados++;
    }

    for (int i = 0; i < creados; i++)
        wait(NULL);

    printf("\n=== Resumen ===\n");
    printf("Autos atendidos         : %d de %d\n", est->atendidos, creados);
    printf("Maximo dentro a la vez  : %d (capacidad %d)\n", est->max_real, espacios);
    printf("Choques por un espacio  : %d\n", est->choques);
    printf("Contador al final       : %d (deberia ser 0)\n", est->ocupados);

    int errores = 0;
    if (est->max_real > espacios) {
        printf("ERROR: se supero la capacidad del estacionamiento\n");
        errores++;
    }
    if (est->choques > 0) {
        printf("ERROR: dos autos usaron el mismo espacio\n");
        errores++;
    }
    if (est->ocupados != 0) {
        printf("ERROR: el contador quedo inconsistente (actualizaciones perdidas)\n");
        errores++;
    }
    if (errores == 0)
        printf("Esta vez no se noto el error; ejecute de nuevo (la carrera depende del tiempo)\n");

    munmap(est, sizeof *est);
    return EXIT_SUCCESS;
}
