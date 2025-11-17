GCC = gcc
CFLAGS = -lm
POSIX = -lpthread

PROGRAMAS = agente controlador

All: $(PROGRAMAS)

agente:
	$(GCC) $(CLFAGS) $@.c -o $@

controlador:
	$(GCC) $(CLFAGS) $@.c -o $@

clean:
	$(RM) $(PROGRAMAS)
