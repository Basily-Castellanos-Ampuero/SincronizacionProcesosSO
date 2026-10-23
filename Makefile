CC     = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -pthread
BINS   = estacionamiento sin_sincronizacion

all: $(BINS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

# Version correcta: semaforos POSIX entre procesos
run: estacionamiento
	./estacionamiento 10 3

# Version sin sincronizacion: muestra la condicion de carrera
run-sin: sin_sincronizacion
	./sin_sincronizacion 10 3

comparar: $(BINS)
	-./sin_sincronizacion 10 3
	@echo
	./estacionamiento 10 3

clean:
	rm -f $(BINS)

.PHONY: all run run-sin comparar clean
