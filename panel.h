/*
 * Panel de la terminal para la simulacion del estacionamiento.
 *
 * Este modulo SOLO dibuja: muestra cada zona como una matriz de espacios
 * (un caracter por espacio), los carriles de entrada y salida, los autos
 * que esperan y los ultimos eventos. No forma parte de la sincronizacion
 * del estacionamiento; la usan igual las dos versiones.
 *
 * Si la salida no es una terminal (por ejemplo "./estacionamiento | cat")
 * en lugar del panel se imprime un evento por linea.
 */
#ifndef PANEL_H
#define PANEL_H

#include <time.h>
#include "zonas.h"

#define MAX_EVENTOS 4096          /* registro circular: se guardan los ultimos */

/* Estado de cada auto */
enum {
    EN_CAMINO,        /* todavia no llega (su proceso aun no existe)   */
    ESPERA_ESPACIO,   /* el centro comercial esta lleno               */
    ESPERA_ENTRADA,   /* hay lugar, pero todas las barreras ocupadas  */
    ENTRANDO,         /* cruzando una barrera de entrada              */
    BUSCANDO,         /* manejando hacia su zona                      */
    ESTACIONADO,
    YENDO_SALIDA,     /* dejo su lugar y maneja hacia la salida       */
    ESPERA_SALIDA,    /* todas las barreras de salida ocupadas        */
    SALIENDO,         /* cruzando una barrera de salida               */
    SE_FUE
};

/* Tipo de evento: define el color con que se muestra */
enum { EV_LLEGA, EV_ESPERA, EV_ENTRA, EV_SALE, EV_ERROR };

typedef struct {
    double   t;
    int      tipo;
    unsigned seq;             /* numero de evento + 1 cuando ya esta completo */
    char     texto[88];
} Evento;

/* Registro de eventos en memoria compartida. Cada proceso reserva su
 * casilla con una suma atomica, asi el registro nunca se mezcla. */
typedef struct {
    struct timespec inicio;
    unsigned total;
    Evento   ev[MAX_EVENTOS];
} Registro;

/* Foto del estacionamiento que el padre toma para dibujar */
typedef struct {
    int autos;                                 /* autos de la simulacion */
    int con_semaforos;                         /* 0 en la version sin sincronizacion */
    int estado[MAX_AUTOS + 1];
    int espacio_de[MAX_AUTOS + 1];             /* espacio de cada auto, -1 = ninguno */
    int carril_entrada[CARRILES_ENTRADA];      /* auto en cada carril, 0 = libre */
    int carril_salida[CARRILES_SALIDA];
    int contador_total;                        /* variable "ocupados" */
    int contador_zona[NUM_ZONAS];              /* variable "ocupados" de cada zona */
    int sem_espacios, sem_entrada, sem_salida; /* valor actual de los semaforos */
    int sem_zona[NUM_ZONAS];
} Foto;

/* Letra con la que se dibuja cada auto (A-Z, se repite cada 26) */
#define LETRA(id) ((char)('A' + ((id) - 1) % 26))

void   registro_iniciar(Registro *r);
double registro_tiempo(const Registro *r);
void   registrar(Registro *r, int tipo, const char *fmt, ...);

int    panel_iniciar(const char *titulo, int alerta);   /* devuelve 1 si es terminal */
void   panel_dibujar(const Foto *f, const Registro *r);
void   panel_volcar(const Registro *r);                 /* modo texto */
void   panel_terminar(void);
void   panel_resumen(void);
void   panel_resultado(int ok, const char *fmt, ...);
void   panel_tabla_zonas(const int *maximo, const double *primer_auto,
                         const double *lleno_desde);    /* tiempos < 0 = nunca */
const char *hora_real(double t, char *buf);             /* buf de 32 bytes */

#endif
