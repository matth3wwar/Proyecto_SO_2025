/****************************************************
* Pontificia Universidad Javeriana
* Autor: Mateo David Guerra
* Fecha: 11 de Noviembre del 2025
* Materia: Sistemas Operativos
* Tema: Directorios
* Descripción: Este código solicita una carpeta para
* mostrar los archivos presentes en la dirección dada
*****************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

static Agent *cabeza_agente = NULL;


pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t hora_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t agents_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hora_cond = PTHREAD_COND_INITIALIZER;

void add_or_update_agent(const char *name, const char *pipeName) {
	pthread_mutex_lock(&agents_mutex);
	char *Agente = cabeza_agente;
	Agente *p = cabeza_agente;
	while (p) {
		if (strcmp(p->name, name) == 0) {
			strncpy(p->pipeName, pipeName, sizeof(p->pipeName)-1);
			p->pipeName[sizeof(p->pipeName)-1] = '\0';
			pthread_mutex_unlock(&agents_mutex);
			return;
		}
	p = p->next;
	}
	Agent *a = malloc(sizeof(Agent));
	if (!a) { perror("malloc"); pthread_mutex_unlock(&agents_mutex); return; }
	strncpy(a->name, name, sizeof(a->name)-1);
	a->name[sizeof(a->name)-1] = '\0';
	strncpy(a->pipeName, pipeName, sizeof(a->pipeName)-1);
	a->pipeName[sizeof(a->pipeName)-1] = '\0';
	a->next = agents_head;
	agents_head = a;
	pthread_mutex_unlock(&agents_mutex);
}

int ejecutarParque(int horaI, int horaF, int segundosH, int total, char *pipeR) {
	int horaActual = horaI * segundosH;
	pthread_mutex_t hora_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t agents_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t hora_cond = PTHREAD_COND_INITIALIZER;
	Agent *agents_head = NULL;
	volatile sig_atomic_t running = 1;
	
}

int main(int argc, char *argv[]) {

	int horaInicio = 7;
	int horaFin = 19;
	int segundosHora = 10;
	int total = 100;
	char *pipeRecibe = "PIPE_RECEPTOR";
	char *endptr;
	//int horaInicioConfig = 0;
	size_t _i;

	for (_i = 1; _i < argc; _i++) {
		if (strcmp(argv[_i], "-i") == 0 || strcmp(argv[_i], "--horaInicio") == 0) {
			_i++;
			int i = strtol(argv[_i], &endptr, 10);
                        if (i < 1 || i > 24) {
				printf("El argumento de hora de inicio \"%s\" no es válido, debe ser un número entero entre 1 y 24 inclusivo\n", argv[_i]);
				return 1;
			}
			horaInicio = i;
		}
		else if (strcmp(argv[_i], "-f") == 0 || strcmp(argv[_i], "--horaFin") == 0) {
			_i++;
			int f = strtol(argv[_i], &endptr, 10);
                        if (f <= horaInicio || f > 24) {
				printf("El argumento de hora de fin \"%s\" no es válido, debe ser un número entero entre la hora de inicio (%d) y 24\n", argv[_i], horaInicio);
				return 1;
			}
			horaFin = f;
		}
		else if (strcmp(argv[_i], "-s") == 0 || strcmp(argv[_i], "--segundosHora") == 0) {
			_i++;
			int s = strtol(argv[_i], &endptr, 10);
                        if (s <= 0) {
				printf("El argumento de segundos en hora \"%s\" no es válido, sebe ser un número entero positivo mayor a 0\n", argv[_i]);
				return 1;
			}
			segundosHora = s;
		}
		else if (strcmp(argv[_i], "-t") == 0 || strcmp(argv[_i], "--total") == 0) {
			_i++;
			int t = strtol(argv[_i], &endptr, 10);
                        if (t <= 0) {
				printf("El argumento de total de personas \"%s\" no es válido, sebe ser un número entero positivo mayor a 0\n", argv[_i]);
				return 1;
			}
			total = t;
		}
		else if (strcmp(argv[_i], "-p") == 0 || strcmp(argv[_i], "--pipeRecibe") == 0) {
			_i++;
                        if (argv[_i] == NULL) {
				printf("El argumento de nombre de pipe \"%s\" no es válido\n", argv[_i]);
				return 1;
			}
			pipeRecibe = argv[_i];
		}
		else {
			printf("El argumento \"%s\" no es válido\nUtilice '-h' para ver la lista de comandos", argv[_i]);
			return 1;
		}
	}
	printf("\nInicializando la simulación con las variables ingresadas\n\n");
	printf("hora inicio: '%d'\n", horaInicio);
	printf("hora fin: '%d'\n", horaFin);
	printf("segundos en hora de simulación: '%d'\n", segundosHora);
	printf("total de personas: '%d'\n", total);
	printf("Nombre de Pipe Receptora: '%s'\n", pipeRecibe);

}

/*
#define FIFO_FILE "/tmp/fifo_twoway"
void reverse_string(char *);
int main() {
   int fd;
   char readbuf[80];
   char end[10];
   int to_end;
   int read_bytes;
   
   /* Create the FIFO if it does not exist 
   mkfifo(FIFO_FILE, S_IFIFO|0640);
   strcpy(end, "end");
   fd = open(FIFO_FILE, O_RDWR);
   while(1) {
      read_bytes = read(fd, readbuf, sizeof(readbuf));
      readbuf[read_bytes] = '\0';
      printf("FIFOSERVER: Received string: \"%s\" and length is %d\n", readbuf, (int)strlen(readbuf));
      to_end = strcmp(readbuf, end);
      
      if (to_end == 0) {
         close(fd);
         break;
      }
      reverse_string(readbuf);
      printf("FIFOSERVER: Sending Reversed String: \"%s\" and length is %d\n", readbuf, (int) strlen(readbuf));
      write(fd, readbuf, strlen(readbuf));
      /*
      sleep - This is to make sure other process reads this, otherwise this
      process would retrieve the message
      
      sleep(2);
   }
   return 0;
}

void reverse_string(char *str) {
   int last, limit, first;
   char temp;
   last = strlen(str) - 1;
   limit = last/2;
   first = 0;
   
   while (first < last) {
      temp = str[first];
      str[first] = str[last];
      str[last] = temp;
      first++;
      last--;
   }
   return;
}
*/
