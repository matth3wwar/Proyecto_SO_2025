GCC = gcc
CFLAGS = -lm
POSIX = -lpthread

PROGRAMAS = cliente servidor

All: $(PROGRAMAS)

cliente:
	$(GCC) $(CLFAGS) $@.c -o $@

servidor:
	$(GCC) $(CLFAGS) $@.c -o $@

clean:
	$(RM) $(PROGRAMAS)
