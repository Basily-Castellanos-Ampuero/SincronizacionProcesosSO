**UNIVERSIDAD NACIONAL DE SAN AGUSTÍN FACULTAD DE INGENIERÍA DE PROCESOS Y SERVICIOS ESCUELA PROFESIONAL DE INGENIERÍA DE SISTEMAS** 



<!-- Start of picture text -->
é(SaERSIT,<br>©<br>‘© ‘2D<br>= 2<br>= © z<br>x ¢ a<br>ie S<br>Q Sy<br>S S<br>COtAys sapres<br><!-- End of picture text -->

# **Avance de proyecto** 

## **SISTEMA SINCRONIZACIÓN DE PROCESOS** 

**Docente:** Delgado Bastidas Jose Rafael 

**Integrantes:** Mejia Rondan Giovanni Patrick Guillen Valcarcel Daniel Josue Cari Quispe Juan Manuel Subia Huaicane Edson Fabricio Mengoa Valeriano Brigitte Noelia 

**Arequipa - 2026** 

### **ÍNDICE** 

|**1. INTRODUCCIÓN**................................................................................................................3|
|---|
|**2. PLANTEAMIENTO DEL PROBLEMA**...........................................................................3|
|2.1. Descripción del problema..............................................................................................3|
|2.2. Justificación...................................................................................................................4|
|2.3. Alcance y limitaciones..................................................................................................4|
|**3. OBJETIVOS**......................................................................................................................... 4|
|3.1. Objetivo general............................................................................................................4|
|3.2. Objetivos específicos.....................................................................................................4|
|**4. MARCO TEÓRICO**.............................................................................................................5|
|4.1. Procesos e hilos.............................................................................................................5|
|4.2. Sincronización de procesos...........................................................................................5|
|4.3. Sección crítica y condición de carrera...........................................................................5|
|4.4. Exclusión mutua y mutex..............................................................................................5|
|4.5. Variables de condición..................................................................................................5|
|4.6. Problema del productor-consumidor.............................................................................6|
|4.7. Herramientas de C++ utilizadas....................................................................................6|
|**5. DESCRIPCIÓN DEL SISTEMA PROPUESTO**..............................................................6|
|5.1. Idea general...................................................................................................................6|
|5.2. Procesos (hilos) a crear..................................................................................................6|
|5.3. Recursos compartidos....................................................................................................6|
|5.4. Mecanismos de sincronización......................................................................................7|
|5.5. Flujo de ejecución.........................................................................................................7|
|5.6. Ejemplo de salida esperada...........................................................................................8|
|5.7. Parámetros configurables..............................................................................................8|
|5.8. Versión sin sincronización (extra opcional)..................................................................8|
|5.9. Conceptos que se explican............................................................................................8|
|**6. METODOLOGÍA**................................................................................................................8|
|6.1. Tipo de investigación....................................................................................................8|
|6.2. Fases del desarrollo.......................................................................................................9|
|6.3. Herramientas...............................................................................................................10|
|**REFERENCIAS BIBLIOGRÁFICAS**.................................................................................11|



2 

### **1. INTRODUCCIÓN** 

Hoy en día casi cualquier sistema informático hace varias cosas al mismo tiempo: un navegador abre varias pestañas, un servidor web atiende a miles de usuarios y una base de datos responde consultas de forma simultánea. Cuando estas tareas comparten recursos, como memoria, archivos o simples contadores, es necesario coordinarlas. Si no se hace, los resultados pueden salir mal de maneras difíciles de detectar. A este reto se le conoce como sincronización de procesos y es uno de los temas centrales de la asignatura de Sistemas Operativos. 

Este proyecto presenta TicketStorm, un programa en C++ que simula un servidor de venta de entradas para un concierto muy popular. En la simulación, un hilo recibe las peticiones de compra y las encola, mientras varios hilos “servidor” las procesan en paralelo. Las entradas disponibles son un recurso compartido que se debe proteger para no vender más de las que existen. 

Elegimos este escenario porque es una situación que todos conocemos y porque permite mostrar, con mensajes en consola, los conceptos más importantes del tema: exclusión mutua, condición de carrera, variables de condición y el problema del productor-consumidor. 

El documento se organiza de la siguiente manera: primero se plantea el problema y se justifica el proyecto; luego se definen los objetivos y se presenta el marco teórico; después se describe el sistema propuesto y, finalmente, se explica la metodología que se seguirá para desarrollarlo y probarlo. 

### **2. PLANTEAMIENTO DEL PROBLEMA** 

#### **2.1. Descripción del problema** 

Cuando sale a la venta un concierto popular, llegan cientos de peticiones de compra casi al mismo tiempo. El servidor debe repartir ese trabajo entre varios nodos para no saturarse y, a la vez, no puede vender más entradas de las que tiene disponibles. Esto genera dos problemas de sincronización: 

- Si la cola de espera se llena, el que genera peticiones debe esperar; si está vacía, los que atienden deben esperar sin gastar procesador. 

- Si dos hilos leen “queda 1 entrada” al mismo tiempo, ambos pueden venderla y el sistema termina vendiendo de más (condición de carrera). 

3 

#### **2.2. Justificación** 

- **Académica:** permite aplicar la teoría de sincronización de procesos en un programa que realmente se ejecuta. 

- **Didáctica:** el escenario es cotidiano y la salida en consola cuenta la historia de la ejecución, lo que facilita la explicación. 

- **Técnica:** usa herramientas estándar de C++ (hilos, mutex y variables de condición), sin librerías externas, por lo que es simple de implementar y de probar. 

#### **2.3. Alcance y limitaciones** 

El proyecto es una simulación. No utiliza red real, base de datos ni interfaz gráfica: las peticiones son datos generados al azar por el programa y los resultados se muestran por consola. El objetivo no es construir un sistema de ventas real, sino demostrar el comportamiento de los mecanismos de sincronización. 

### **3. OBJETIVOS** 

#### **3.1. Objetivo general** 

Demostrar cómo los mecanismos de sincronización de procesos (mutex y variables de condición) permiten coordinar hilos que comparten una cola de trabajo y un contador de recursos, mediante una simulación en C++. 

#### **3.2. Objetivos específicos** 

1. Implementar el patrón productor-consumidor con una cola de capacidad limitada. 

2. Proteger un contador compartido (entradas disponibles) para evitar condiciones de carrera. 

3. Mostrar el bloqueo y el despertar de los hilos mediante mensajes en consola. 

4. Medir cuántas peticiones atiende cada hilo servidor. 

### **4. MARCO TEÓRICO** 

#### **4.1. Procesos e hilos** 

Un proceso es un programa en ejecución con su propio espacio de memoria. Un hilo ( _thread_ ) es una unidad de ejecución dentro de un proceso; todos los hilos de un mismo proceso 

4 

comparten la misma memoria. Esto los hace más ligeros y rápidos de comunicar, pero también significa que pueden pisarse entre sí si acceden a los mismos datos sin control (Silberschatz et al., 2018; Tanenbaum & Bos, 2015). 

#### **4.2. Sincronización de procesos** 

La sincronización consiste en coordinar el orden en que varios hilos o procesos acceden a recursos compartidos. Es necesaria porque, cuando varios hilos modifican los mismos datos, el resultado puede depender del orden en que el sistema operativo los vaya ejecutando, y ese orden no es predecible. 

#### **4.3. Sección crítica y condición de carrera** 

La sección crítica es la parte del código donde un hilo accede a un recurso compartido y que no debe ser ejecutada por más de un hilo a la vez. Una condición de carrera ocurre cuando el resultado final depende del orden en que los hilos ejecutan esa sección. Un ejemplo típico es un contador: dos hilos leen el valor 1, ambos lo disminuyen y ambos creen haber tomado la última unidad. 

#### **4.4. Exclusión mutua y mutex** 

La exclusión mutua garantiza que solo un hilo esté dentro de la sección crítica en un momento dado. En C++ se implementa con std::mutex: un hilo lo bloquea al entrar y lo libera al salir, y los demás esperan su turno. Para evitar olvidar la liberación se usan envolturas como std::unique_lock o std::lock_guard, que liberan el mutex automáticamente al terminar el bloque (Williams, 2019). 

#### **4.5. Variables de condición** 

Una variable de condición permite que un hilo espere de forma pasiva, sin consumir procesador, hasta que se cumpla una condición y otro hilo lo despierte. Su base teórica proviene de los monitores propuestos por Hoare (1974). En C++ se usa std::condition_variable junto con un mutex. Conviene esperar siempre con un predicado, porque un hilo puede despertarse sin que nadie lo haya notificado (despertar espurio). 

#### **4.6. Problema del productor-consumidor** 

Es un problema clásico planteado por Dijkstra (1965), también conocido como el problema del buffer limitado. Unos hilos (productores) generan datos y los guardan en un buffer compartido de tamaño fijo, y otros (consumidores) los toman para procesarlos. Hay que cuidar tres 

5 

situaciones: que dos hilos no modifiquen el buffer a la vez, que el productor espere si el buffer está lleno y que el consumidor espere si está vacío (Downey, 2016). 

#### **4.7. Herramientas de C++ utilizadas** 

Tabla 1. _Herramientas de C++ y su uso en el proyecto_ 

|**Herramienta**|**Uso en elproyecto**|
|---|---|
|std::thread|Crear los hilos DispatcheryWorkers|
|std::mutex|Proteger la cola,el contador de entradasyla consola|
|std::unique_lock|Bloquearyliberar el mutex de forma segura|
|std::condition_variable|Hacer esperar a los hilos cuando la cola está vacía o llena|
|std::queue|Almacenar laspeticionespendientes|
|ThreadSanitizer(opcional)|Detectar la condición de carrera de la versión sin mutex|



### **5. DESCRIPCIÓN DEL SISTEMA PROPUESTO** 

#### **5.1. Idea general** 

TicketStorm simula el momento en que sale a la venta un concierto popular. Un hilo (Dispatcher) genera peticiones del tipo “Usuario 17 quiere 2 entradas” y las coloca en una cola de capacidad limitada. Varios hilos (Workers) las toman de la cola, las procesan y descuentan las entradas del stock si todavía alcanzan; en caso contrario, responden “agotado”. 

#### **5.2. Procesos (hilos) a crear** 

Tabla 2. _Hilos del sistema_ 

|**Hilo**|**Cantidad**|**Rol**|
|---|---|---|
|Dispatcher|1|Genera las peticiones de compra y las encola|
|||(productor)|
|Worker|3 o 4|Tomapeticiones de la colaylasprocesa(consumidor)|



#### **5.3. Recursos compartidos** 

- Cola de peticiones, con una capacidad máxima (por ejemplo, 5). 

- Contador de entradas disponibles (por ejemplo, 50). 

- Salida en consola, protegida con un mutex para que los mensajes de distintos hilos no se mezclen. 

6 

#### **5.4. Mecanismos de sincronización** 

Tabla 3. _Problemas de sincronización y su solución_ 

|**Problema**|**Solución**|
|---|---|
|Acceso simultáneo a la cola|std::mutexconstd::unique_lock|
|Cola vacía: el Worker debe esperar|condition_variable“haytrabajo”|
|Cola llena: el Dispatcher debe esperar|condition_variable“hayespacio”|
|Dos Workers modifican las entradas a la vez|std::mutex propiopara el contador de entradas|
|Terminar elprograma limpiamente|Bandera “finalizado”y notify_all|



#### **5.5. Flujo de ejecución** 

1. El programa inicia y crea los Workers y el Dispatcher. 

2. El Dispatcher genera un total de peticiones (por ejemplo, 30), cada una con un usuario y una cantidad aleatoria de entradas (de 1 a 4). 

3. Si la cola está llena, el Dispatcher se bloquea hasta que un Worker libere espacio. 

4. Un Worker despierta, toma una petición y simula el procesamiento con una pausa (sleep) de duración aleatoria. 

5. El Worker revisa el contador: si hay entradas suficientes, las descuenta y confirma la compra; si no, responde “agotado”. 

6. Al terminar las peticiones, el Dispatcher activa la bandera de finalizado y despierta a todos los Workers. 

7. Se imprime un resumen final con las peticiones atendidas por cada Worker, las entradas vendidas y las peticiones rechazadas. 

#### **5.6. Ejemplo de salida esperada** 

A modo de ejemplo, la consola debería mostrar mensajes como los siguientes (los valores son 

ilustrativos y cambiarán en cada ejecución): 

<mark>[Dispatcher] Petición #1: Usuario 17 solicita 2 entradas (cola: 1/5) [Worker 2] Atendiendo petición #1... [Worker 2] Compra confirmada. Entradas restantes: 48 [Dispatcher] Cola llena, esperando espacio... [Worker 1] Petición #9: entradas agotadas para Usuario 4 ... === RESUMEN === Worker 1: [</mark> __ <mark>] peticiones | Worker 2: [</mark> __ <mark>] | Worker 3: [</mark> __ <mark>] Entradas vendidas: [</mark> __ <mark>] | Peticiones rechazadas: [</mark> __ <mark>]</mark> 

7 

#### **5.7. Parámetros configurables** 

El programa permitirá modificar el número de Workers, la capacidad de la cola, el total de entradas y el total de peticiones. Cambiar estos valores sirve para mostrar comportamientos distintos, por ejemplo, una cola que se llena constantemente cuando hay pocos Workers. 

#### **5.8. Versión sin sincronización (extra opcional)** 

Como complemento, se propone una segunda ejecución sin mutex en el contador de entradas. En esa versión se espera que aparezcan ventas de más (condición de carrera). Luego se compara con la versión sincronizada, donde las entradas vendidas nunca superan las disponibles. Este contraste permite explicar con claridad para qué sirve cada mecanismo. 

#### **5.9. Conceptos que se explican** 

Con este sistema se pueden explicar los siguientes conceptos: exclusión mutua, sección crítica, condición de carrera, productor-consumidor, espera pasiva con variables de condición y terminación segura de hilos. 

### **6. METODOLOGÍA** 

#### **6.1. Tipo de investigación** 

El proyecto es de tipo aplicado y de enfoque experimental por simulación. Se construye un modelo en software de un problema clásico de sincronización y luego se comprueba su comportamiento bajo distintas configuraciones. 

#### **6.2. Fases del desarrollo** 

##### **Fase 1: Análisis y revisión teórica** 

Se estudia el problema del productor-consumidor, las condiciones de carrera y los mecanismos de sincronización de C++ (std::thread, std::mutex y std::condition_variable). 

##### **Fase 2: Diseño** 

- Definir los hilos, los recursos compartidos y qué mecanismo protege cada uno. 

- Elaborar un diagrama de flujo o de secuencia del Dispatcher y los Workers. 

- Definir los parámetros configurables. 

8 

##### **Fase 3: Implementación** 

El desarrollo será incremental, en C++17, siguiendo este orden: 

1. Cola protegida con mutex. 

2. Condiciones de cola llena y cola vacía. 

3. Contador de entradas con su propio mutex. 

4. Terminación limpia y resumen final. 

5. Versión sin sincronización para el contraste. 

##### **Fase 4: Pruebas y validación** 

Se realizarán las pruebas de la Tabla 4 para verificar que el sistema funciona correctamente. 

Tabla 4. _Plan de pruebas_ 

|**Prueba**|**Qué se verifica**|
|---|---|
|Ejecución base(3 Workers,cola de 5)|Funcionamiento correctoysalida coherente|
|Colapequeña conpocos Workers|El Dispatcher se bloqueayse reanuda|
|Muchos Workers|Los Workers esperan sin consumirprocesador|
|Versión sin mutex en el contador|Aparecen ventas de más(condición de carrera)|
|Versión con mutex|Las entradas vendidas nunca superan las<br>disponibles|
|Repetir 10 ejecuciones|El resultado es consistente, aunque el orden de los<br>mensajes varíe|



##### **Fase 5: Análisis de resultados y conclusiones** 

Se comparan la versión sincronizada y la no sincronizada, y se explica por qué el orden de los mensajes cambia entre ejecuciones: los hilos no se ejecutan en un orden determinista. 

#### **6.3. Herramientas** 

- Lenguaje: C++17. 

- Compilador: g++ con la opción -pthread (o MSVC). 

- Detección de carreras (opcional): ThreadSanitizer (-fsanitize=thread). 

- Diagramas: draw.io o similar. 

9 

### **REFERENCIAS BIBLIOGRÁFICAS** 

- [1] cppreference.com, "Thread support library." [Online]. Available: https://en.cppreference.com. 

- [2] Clang, "ThreadSanitizer." [Online]. Available: https://clang.llvm.org/docs/ThreadSanitizer.html. 

- [3] E. W. Dijkstra, "Cooperating sequential processes (EWD123)," Technische Hogeschool Eindhoven, 1965. 

- [4] A. B. Downey, _The Little Book of Semaphores_ , 2nd ed. Green Tea Press, 2016. 

- [5] C. A. R. Hoare, "Monitors: An operating system structuring concept," _Commun. ACM_ , vol. 17, no. 10, pp. 549–557, 1974, doi: 10.1145/355620.361161. 

- [6] A. Silberschatz, P. B. Galvin, and G. Gagne, _Operating System Concepts_ , 10th ed. Wiley, 2018. 

- [7] A. S. Tanenbaum and H. Bos, _Modern Operating Systems_ , 4th ed. Pearson, 2015. 

- [8] A. Williams, _C++ Concurrency in Action_ , 2nd ed. Manning Publications, 2019. 

10 

