/*
 * Sincronizacion de procesos: el MISMO centro comercial pero SIN semaforos.
 *
 * Sirve para demostrar la condicion de carrera. Cada auto:
 *   1. mira el cartel de lugares libres (lee "ocupados")
 *   2. cruza una barrera (nadie limita cuantos pasan a la vez)
 *   3. elige la zona mas comoda que "parece" tener lugar (lee "ocupados_zona")
 *   4. maneja hasta la zona, elige un lugar libre y lo ocupa (escribe)
 * Entre la lectura y la escritura pasan varios segundos y otros autos
 * hacen lo mismo. Resultado: las zonas comodas se llenan de mas, dos autos
 * terminan en el mismo lugar y los contadores quedan mal.
 *
 * Los campos "dentro_zona", "max_zona", "choques" y "atendidos" son un
 * medidor externo (operaciones atomicas) que solo sirve para comprobar
 * el error; no forman parte de la logica del estacionamiento.
 *
 * Uso: ./sin_sincronizacion [autos]   (por defecto 2000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "zonas.h"
#include "panel.h"
#include "tiempos.h"

#define PAUSA_ARTIFICIAL 1000    /* us: agranda la ventana de leer-modificar-escribir */
#define PACIENCIA        MIN(20) /* tras 20 min esperando, el conductor entra igual */

typedef struct {
    int num_autos;
    volatile int espacio[MAX_ESPACIOS];         /* auto en cada espacio, 0 = libre */
    volatile int ocupados;                      /* contadores SIN proteccion */
    volatile int ocupados_zona[NUM_ZONAS];
    volatile int estado[MAX_AUTOS + 1];         /* solo lo escribe el propio auto */
    volatile int espacio_de[MAX_AUTOS + 1];

    /* Medidor externo (atomico) */
    int dentro_zona[NUM_ZONAS];
    int max_zona[NUM_ZONAS];
    int choques;
    int atendidos;
    double primer_auto[NUM_ZONAS];
    double lleno_desde[NUM_ZONAS];

    Registro registro;
} CentroComercial;

static CentroComercial *cc;

static void medir_entrada(int z)
{
    int dentro = __atomic_add_fetch(&cc->dentro_zona[z], 1, __ATOMIC_SEQ_CST);
    int max = __atomic_load_n(&cc->max_zona[z], __ATOMIC_SEQ_CST);
    while (dentro > max &&
           !__atomic_compare_exchange_n(&cc->max_zona[z], &max, dentro, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        ;
    if (cc->primer_auto[z] < 0)
        cc->primer_auto[z] = registro_tiempo(&cc->registro);
    if (dentro >= ZONAS[z].capacidad && cc->lleno_desde[z] < 0)
        cc->lleno_desde[z] = registro_tiempo(&cc->registro);
}

static void auto_proceso(int id, double llegada)
{
    Registro *r = &cc->registro;
    char letra = LETRA(id);
    int total = total_espacios();
    srand(getpid() ^ time(NULL));

    /* Esperar la hora exacta de llegada de este auto */
    double falta = llegada - registro_tiempo(r);
    if (falta > 0)
        usleep((useconds_t)(falta * 1e6));

    registrar(r, EV_LLEGA, "Auto %d [%c] llega (PID %d)", id, letra, getpid());

    /* 1. Mirar el cartel de lugares libres: lectura sin proteccion */
    if (cc->ocupados >= total) {
        cc->estado[id] = ESPERA_ESPACIO;
        registrar(r, EV_ESPERA, "Auto %d [%c] espera: el cartel dice lleno", id, letra);
        useconds_t esperado = 0;
        while (cc->ocupados >= total && esperado < SEG(PACIENCIA)) {
            usleep(SEG(60));                    /* espera activa: vuelve a mirar */
            esperado += SEG(60);
        }
        if (cc->ocupados >= total)
            registrar(r, EV_ESPERA, "Auto %d [%c] se cansa de esperar y entra igual", id, letra);
    }

    /* 2. Cruzar la barrera: no hay control, pueden pasar muchos a la vez */
    cc->estado[id] = ENTRANDO;
    usleep(variar(T_BARRERA));

    /* 3. Elegir la zona mas comoda que "parece" tener lugar */
    int z = NUM_ZONAS - 1;
    for (int i = 0; i < NUM_ZONAS; i++) {
        if (cc->ocupados_zona[i] < ZONAS[i].capacidad) {
            z = i;
            break;
        }
    }
    cc->estado[id] = BUSCANDO;
    registrar(r, EV_ENTRA, "Auto %d [%c] va a %s", id, letra, ZONAS[z].nombre);

    /* 4. Manejar hasta la zona: mientras tanto otros eligen lo mismo */
    usleep(variar(ZONAS[z].recorrido));

    /* 5. Elegir uno de los lugares que "parecen" libres y estacionar.
     *    Si no ve ninguno, se mete en cualquiera. */
    int inicio = zona_inicio(z), capacidad = ZONAS[z].capacidad;
    int libres = 0;
    for (int e = inicio; e < inicio + capacidad; e++)
        if (cc->espacio[e] == 0)
            libres++;
    int mi_espacio = inicio + rand() % capacidad;
    if (libres > 0) {
        int elegido = rand() % libres;
        for (int e = inicio; e < inicio + capacidad; e++) {
            if (cc->espacio[e] == 0 && elegido-- == 0) {
                mi_espacio = e;
                break;
            }
        }
    }
    usleep(variar(T_MANIOBRA));                    /* tiempo de maniobra */
    int otro = cc->espacio[mi_espacio];
    if (otro != 0) {
        __atomic_add_fetch(&cc->choques, 1, __ATOMIC_SEQ_CST);
        registrar(r, EV_ERROR, "CHOQUE: auto %d [%c] y auto %d en %s, lugar %d",
                  id, letra, otro, ZONAS[z].nombre, mi_espacio - inicio + 1);
    }
    cc->espacio[mi_espacio] = id;
    cc->espacio_de[id] = mi_espacio;

    /* ocupados++ en tres pasos (leer, modificar, escribir): se pierden
     * actualizaciones si otro proceso escribe en medio */
    int tmp = cc->ocupados;
    usleep(PAUSA_ARTIFICIAL);
    cc->ocupados = tmp + 1;
    tmp = cc->ocupados_zona[z];
    usleep(PAUSA_ARTIFICIAL);
    cc->ocupados_zona[z] = tmp + 1;
    medir_entrada(z);
    cc->estado[id] = ESTACIONADO;

    /* 6. De compras */
    usleep(entre(T_ESTADIA_MIN, T_ESTADIA_MAX));

    /* 7. Salir: mismas operaciones sin proteccion */
    __atomic_sub_fetch(&cc->dentro_zona[z], 1, __ATOMIC_SEQ_CST);
    if (cc->espacio[mi_espacio] == id)
        cc->espacio[mi_espacio] = 0;
    cc->espacio_de[id] = -1;
    cc->estado[id] = YENDO_SALIDA;
    tmp = cc->ocupados;
    usleep(PAUSA_ARTIFICIAL);
    cc->ocupados = tmp - 1;
    tmp = cc->ocupados_zona[z];
    usleep(PAUSA_ARTIFICIAL);
    cc->ocupados_zona[z] = tmp - 1;
    registrar(r, EV_SALE, "Auto %d [%c] deja %s", id, letra, ZONAS[z].nombre);

    usleep(variar(ZONAS[z].recorrido));
    cc->estado[id] = SALIENDO;
    usleep(variar(T_BARRERA));
    cc->estado[id] = SE_FUE;
    __atomic_add_fetch(&cc->atendidos, 1, __ATOMIC_SEQ_CST);
}

/* Sin semaforos el padre solo puede copiar lo que haya en ese instante */
static void tomar_foto(Foto *f)
{
    f->autos = cc->num_autos;
    f->con_semaforos = 0;
    for (int id = 1; id <= cc->num_autos; id++) {
        f->estado[id] = cc->estado[id];
        f->espacio_de[id] = cc->espacio_de[id];
    }
    f->contador_total = cc->ocupados;
    for (int z = 0; z < NUM_ZONAS; z++)
        f->contador_zona[z] = cc->ocupados_zona[z];
}

static int comparar_tiempos(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void al_interrumpir(int sig)
{
    (void)sig;
    panel_terminar();
    _exit(130);
}

int main(int argc, char *argv[])
{
    int autos = (argc > 1) ? atoi(argv[1]) : 2000;
    int total = total_espacios();

    if (autos < 1 || autos > MAX_AUTOS) {
        fprintf(stderr, "Uso: %s [autos 1-%d]\n", argv[0], MAX_AUTOS);
        return EXIT_FAILURE;
    }
    if (total > MAX_ESPACIOS) {
        fprintf(stderr, "Las zonas suman %d espacios; el maximo es %d\n", total, MAX_ESPACIOS);
        return EXIT_FAILURE;
    }

    cc = mmap(NULL, sizeof *cc, PROT_READ | PROT_WRITE,
              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (cc == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    cc->num_autos = autos;
    for (int id = 1; id <= autos; id++)
        cc->espacio_de[id] = -1;
    for (int z = 0; z < NUM_ZONAS; z++)
        cc->primer_auto[z] = cc->lleno_desde[z] = -1;

    /* Mismas llegadas que la version con semaforos */
    double *llegada = malloc(sizeof(double) * autos);
    if (llegada == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    srand(time(NULL));
    for (int i = 0; i < autos; i++)
        llegada[i] = rand() / (RAND_MAX + 1.0) * T_LLEGADAS / ESCALA;
    qsort(llegada, autos, sizeof(double), comparar_tiempos);

    registro_iniciar(&cc->registro);
    signal(SIGINT, al_interrumpir);
    int terminal = panel_iniciar("CENTRO COMERCIAL · SIN SINCRONIZACIÓN", 1);

    static Foto foto;
    int creados = 0, vivos = 0;
    for (;;) {
        double t = registro_tiempo(&cc->registro);
        while (creados < autos && llegada[creados] <= t + ANTICIPO) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                break;
            }
            if (pid == 0) {                              /* HIJO */
                signal(SIGINT, SIG_DFL);
                auto_proceso(creados + 1, llegada[creados]);
                exit(EXIT_SUCCESS);
            }
            creados++;
            vivos++;
        }
        while (waitpid(-1, NULL, WNOHANG) > 0)
            vivos--;

        if (terminal) {
            tomar_foto(&foto);
            panel_dibujar(&foto, &cc->registro);
        } else {
            panel_volcar(&cc->registro);
        }
        if (creados == autos && vivos == 0)
            break;
        usleep(50000);
    }
    panel_terminar();

    int zonas_sobre = 0, contadores_ok = cc->ocupados == 0;
    for (int z = 0; z < NUM_ZONAS; z++) {
        if (cc->max_zona[z] > ZONAS[z].capacidad)
            zonas_sobre++;
        if (cc->ocupados_zona[z] != 0)
            contadores_ok = 0;
    }

    panel_resumen();
    panel_tabla_zonas(cc->max_zona, cc->primer_auto, cc->lleno_desde);
    panel_resultado(cc->atendidos == creados, "Autos atendidos: %d de %d", cc->atendidos, creados);
    panel_resultado(zonas_sobre == 0, "Zonas que superaron su capacidad: %d", zonas_sobre);
    panel_resultado(cc->choques == 0, "Choques por un mismo lugar: %d", cc->choques);
    panel_resultado(contadores_ok, "Contadores al final: ocupados = %d (debería ser 0)%s",
                    cc->ocupados, contadores_ok ? "" : " → actualizaciones perdidas");
    if (zonas_sobre == 0 && cc->choques == 0 && contadores_ok)
        printf("\n  Esta vez no se notó el error; ejecute de nuevo "
               "(la carrera depende del tiempo).\n");

    munmap(cc, sizeof *cc);
    free(llegada);
    return EXIT_SUCCESS;
}
