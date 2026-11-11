CC     = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -pthread
BINS   = estacionamiento sin_sincronizacion
DEPS   = panel.c panel.h zonas.h tiempos.h

all: $(BINS)

%: %.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ $< panel.c

# Version correcta: semaforos POSIX entre procesos (2000 autos)
run: estacionamiento
	./estacionamiento

# Version sin sincronizacion: muestra la condicion de carrera
run-sin: sin_sincronizacion
	./sin_sincronizacion

comparar: $(BINS)
	-./sin_sincronizacion
	@echo
	@read -p "  Presione Enter para ver la version con semaforos..." _
	./estacionamiento

clean:
	rm -f $(BINS)

.PHONY: all run run-sin comparar clean
