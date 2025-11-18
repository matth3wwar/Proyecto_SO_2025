GCC = gcc
CFLAGS = -lm
POSIX = -lpthread

PROGRAMAS = agente controlador
PIPES = PIPE_RECEPTOR PIPE_RESP_*

All: $(PROGRAMAS)

agente:
	$(GCC) $(CLFAGS) $@.c -o $@

controlador:
	$(GCC) $(CLFAGS) $@.c -o $@

clean:
	$(RM) $(PROGRAMAS) $(PIPES)
