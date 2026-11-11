/*
 * Tiempos de la simulacion.
 *
 * Todos los valores estan en SEGUNDOS REALES (lo que tardaria en la vida
 * real) y se convierten a la escala de la simulacion con SEG():
 *   escala 1:300  ->  1 s simulado = 300 s reales (5 minutos)
 *   ejemplo: cruzar la barrera tarda 10 s reales = 0,033 s en la simulacion
 *
 * Una tarde de fin de semana en el centro comercial (unas 4 a 5 horas
 * reales) dura alrededor de 1 minuto. Para que la demo vaya mas lenta o
 * mas rapida basta con cambiar ESCALA.
 */
#ifndef TIEMPOS_H
#define TIEMPOS_H

#include <stdlib.h>
#include <unistd.h>

#define ESCALA 300

#define MIN(m) ((m) * 60)                          /* minutos -> segundos */
#define SEG(s) ((useconds_t)((s) * 1000000.0 / ESCALA))

/* Tiempos reales */
#define T_BARRERA      10         /* cruzar una barrera (entrada o salida)   */
#define T_MANIOBRA     30         /* estacionar una vez en la zona           */
#define T_ESTADIA_MIN  MIN(30)    /* tiempo de compras: de media hora ...    */
#define T_ESTADIA_MAX  MIN(180)   /* ... a 3 horas                           */
#define T_LLEGADAS     MIN(90)    /* todos los autos llegan en 1 h 30 min    */

/* Tiempo simulado al azar entre min y max segundos reales */
static inline useconds_t entre(double min, double max)
{
    return SEG(min) + (useconds_t)(rand() % (SEG(max) - SEG(min) + 1));
}

/* Cada conductor es distinto: el tiempo varia +-20 % */
static inline useconds_t variar(double s)
{
    return entre(s * 0.8, s * 1.2);
}

/* El padre crea cada proceso un poco antes de que llegue su auto
 * (como mucho ANTICIPO segundos simulados); el auto espera su hora exacta */
#define ANTICIPO 0.2

#endif
