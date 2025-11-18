/****************************************************
* Pontificia Universidad Javeriana
* Autores: Mateo David Guerra y Ángel Daniel García Santana
* Fecha: Noviembre 2025
* Materia: Sistemas Operativos
* Proyecto: Sistema de Reservas - Cliente
* Tema: Implementación Controlador de Reservas
*****************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

typedef struct {
	int horaI;
	int horaF;
	unsigned int segH;
	/* mutex que protege current_hour y demás estructuras */
	pthread_mutex_t *mutex;
	/* cond variable para notificar al controlador y otros hilos */
	pthread_cond_t  *cond;
	/* puntero a la hora de simulación compartida */
	volatile int *hora_actual;
	/*bandera para pedir terminación (0 = seguir, 1 = terminar) */
	volatile int *terminate;
} RelojArgs;

typedef struct Solicitud {
	char familia[64];
	int hora;
	int personas;
	char nombre_agente[64];
	char pipe_respuesta[128];
	struct Solicitud *siguiente;
} Solicitud;

typedef struct {
	char* pipe_recibe;

	// sincronización y datos compartidos con el controlador
	pthread_mutex_t *mutex_solicitudes;    // protege la cola de solicitudes
	pthread_cond_t  *cond_solicitudes;     // notifica nuevo elemento en la cola
	Solicitud **cola_cabeza;               // puntero a la cabeza de la cola (en memoria compartida)
	Solicitud **cola_final;                // puntero al final de la cola

	// otras variables compartidas
	int *flag_terminar;                    // si es != 0, acaba el hilo
} ArgsAgentes;

// ---- Rutina del hilo que atiende al pipe de los agentes ----
static void *hilo_manejador_agentes(void *vargs) {
	ArgsAgentes *args = (ArgsAgentes *)vargs;
	if (!args) return (void*) -1;

	// Abrir el fifo (named pipe) para lectura. Se asume que el pipe ya fue creado (mkfifo)
	int fd = open(args->pipe_recibe, O_RDONLY);
	if (fd < 0) {
		perror("hilo_manejador_agentes: open pipe_recibe falló");
		return (void*) -1;
	}

	// Convertir a FILE* para poder usar fgets y procesar línea por línea
	FILE *fp = fdopen(fd, "r");
	if (!fp) {
		perror("hilo_manejador_agentes: fdopen falló");
		close(fd);
		return (void*) -1;
	}

	char linea[512];
	while (1) {
		// Checkar terminación periódicamente
		if (args->flag_terminar && *args->flag_terminar) {
			break;
		}

		// Leer una línea (bloqueante hasta que llegue algo)
		if (fgets(linea, sizeof(linea), fp) == NULL) {
			// Si fgets devuelve NULL: puede ser EOF (escritores cerraron) o error.
			if (feof(fp)) {
				// No hay escritores actualmente: esperar y reintentar (evita salida inmediata)
				clearerr(fp);
				sleep(1);
				continue;
			} else {
				perror("hilo_manejador_agentes: fgets error");
				break;
			}
		}

		// Limpiar salto de línea
		size_t L = strlen(linea);
		if (L > 0 && linea[L-1] == '\n') linea[L-1] = '\0';

		// Parsear mensaje simple CSV: tipo, ...
		// Tipos esperados: "REG" (registro) o "REQ" (solicitud)
		char *copia = strdup(linea);
		if (!copia) continue;
		char *token = NULL;
		token = strtok(copia, ",");
		if (!token) { free(copia); continue; }

		if (strcmp(token, "REG") == 0) {
			// Mensaje de registro: REG,NOMBRE_AGENTE,PIPE_RESPUESTA
			char *nombre = strtok(NULL, ",");
			char *pipe_resp = strtok(NULL, ",");

			if (nombre && pipe_resp) {
				// Aquí podrías almacenar info del agente en una tabla; por simplicidad imprimimos
				printf("[ManejadorAgentes] Registro de agente: %s (pipe_resp=%s)\n", nombre, pipe_resp);
				// (Opcional) responder la hora actual por el pipe de respuesta si se desea.
			} else {
				fprintf(stderr, "[ManejadorAgentes] REG mal formado: %s\n", linea);
			}
		}
		else if (strcmp(token, "REQ") == 0) {
			// REQ,FAMILIA,HORA,PERSONAS
			char *familia = strtok(NULL, ",");
			char *hora_s = strtok(NULL, ",");
			char *personas_s = strtok(NULL, ",");
			if (!familia || !hora_s || !personas_s) {
				fprintf(stderr, "[ManejadorAgentes] REQ mal formado: %s\n", linea);
				free(copia);
				continue;
			}

			// Crear estructura solicitud
			Solicitud *s = malloc(sizeof(Solicitud));
			if (!s) {
				perror("[ManejadorAgentes] malloc solicitud");
				free(copia);
				continue;
			}
			memset(s, 0, sizeof(Solicitud));
			strncpy(s->familia, familia, sizeof(s->familia)-1);
			s->hora = atoi(hora_s);
			s->personas = atoi(personas_s);

			// Opcional: si queremos saber qué agente envió, podríamos exigir que el agente
			// incluya su nombre o pipe de respuesta en la REQ. Aquí no lo tenemos; rellenar si aplica.
			strncpy(s->nombre_agente, "desconocido", sizeof(s->nombre_agente)-1);
			s->pipe_respuesta[0] = '\0';
			s->siguiente = NULL;

			// Encolar la solicitud protegido por mutex y notificar al controlador
			pthread_mutex_lock(args->mutex_solicitudes);
			if (*(args->cola_final) == NULL) {
				// cola vacía
				*(args->cola_cabeza) = s;
				*(args->cola_final) = s;
			} else {
				(*(args->cola_final))->siguiente = s;
				*(args->cola_final) = s;
			}
			// Notificar controlador que hay nueva solicitud
			pthread_cond_signal(args->cond_solicitudes);
			pthread_mutex_unlock(args->mutex_solicitudes);

			printf("[ManejadorAgentes] Nueva solicitud encolada: familia=%s hora=%d personas=%d\n", s->familia, s->hora, s->personas);
		}
		else {
			fprintf(stderr, "[ManejadorAgentes] Tipo de mensaje desconocido: %s\n", token);
		}

		free(copia);
	}

	fclose(fp); // cierra fd también
	return (void*) 0;
}

static void *hilo_reloj(void *vargs) {

	RelojArgs *args = (RelojArgs *)vargs;

	if (args->segH == 0) {
		printf("segHoras no puede ser 0\n");
        	return (void *) -1;
	}

	/* Inicializar la hora compartida con horaIni si es necesario */
	pthread_mutex_lock(args->mutex);
	*args->hora_actual = args->horaI;
	pthread_cond_broadcast(args->cond); /* notificar hora inicial si se requiere */
	pthread_mutex_unlock(args->mutex);

	while (1) {
		/* Comprobar si nos pidieron terminar antes de dormir */
		if (args->terminate && *args->terminate) break;

		/* esperar segHoras segundos (representa 1 hora de simulación) */
		sleep(args->segH);

		/* avanzar la hora en la estructura compartida y notificar */
		pthread_mutex_lock(args->mutex);
		if (args->terminate && *args->terminate) {
			printf("[Reloj] el proceso se ha terminado de acuerdo a la solicitud");
			pthread_mutex_unlock(args->mutex);
			break;
		}

		(*args->hora_actual)++;
		/* Si la hora llega > 24 opcionalmente normalizar o según reglas del proyecto */
		/* Imprimir hora (opcional, el controlador puede imprimir también) */
		printf("[Reloj] Hora simulada: %d\n", *args->hora_actual);

		/* Notificar a los hilos que esperan el avance de hora (e.g. controlador) */
		pthread_cond_broadcast(args->cond);
		pthread_mutex_unlock(args->mutex);

		/* Si alcanzamos horaFin, terminamos el hilo reloj */
		if (*args->hora_actual >= args->horaF) break;
	}

	return NULL;
}

int crear_hilo_manejador_agentes(ArgsAgentes *args, pthread_t *out_hilo) {
	if (!args || !args->mutex_solicitudes || !args->cond_solicitudes || !args->cola_cabeza || !args->cola_final || !args->pipe_recibe) {
		fprintf(stderr, "crear_hilo_manejador_agentes: argumentos inválidos\n");
		return -1;
	}

	pthread_t th;
	int rc = pthread_create(&th, NULL, hilo_manejador_agentes, (void*) args);
	if (rc != 0) {
		fprintf(stderr, "crear_hilo_manejador_agentes: pthread_create error %d\n", rc);
		return -1;
	}

	if (out_hilo) *out_hilo = th;
	return 0;
}

int crear_hilo_reloj(RelojArgs *args, pthread_t *salida) {
	if (args == NULL || args->mutex == NULL || args->cond == NULL || args->hora_actual == NULL) {
		return -1;
	}

	if (args->segH == 0) {
		return -1;
	}

	pthread_t reloj;
	int rc = pthread_create(&reloj, NULL, hilo_reloj, (void *)args);
	if (rc != 0) {
		printf("No se pudo crear correctamente el hilo del reloj\n");
		return rc;
	}

	if (salida) *salida = reloj;
	return 0;
}

int ejecutarParque(int horaI, int horaF, int segundosH, int total, char *pipeR) {
	pthread_mutex_t hora_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t hora_cond = PTHREAD_COND_INITIALIZER;

	pthread_mutex_t agentes_mutex = PTHREAD_MUTEX_INITIALIZER;

	int hora_actual = horaI;
	int terminar = 0;
	pthread_t hiloReloj;
	pthread_t hiloAgentes;

	if (mkfifo(pipeR, 0666) < 0) {
//		if (errno != EEXIST) {
//			printf("ya existe la pipe %s", pipeR);
//			return 1;
//		}
//	} else {
		printf("Error al crear el pipe [%s]", pipeR);
	}

	/******************************************
	 * Creación del hilo manejador de agentes
	******************************************/

	Solicitud *cola_cabeza = NULL;
	Solicitud *cola_final = NULL;
	pthread_mutex_t mutex_solicitudes = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond_solicitudes = PTHREAD_COND_INITIALIZER;
	int flag_terminar = 0;

	ArgsAgentes args_ag;  // pipe_recibe (pipeR), mutex_solicitudes, cond_solicitudes, cola_cabeza, cola_final, flag_terminar
	args_ag.pipe_recibe = pipeR;
	args_ag.mutex_solicitudes = &mutex_solicitudes;
	args_ag.cond_solicitudes = &cond_solicitudes;
	args_ag.cola_cabeza = &cola_cabeza;
	args_ag.cola_final = &cola_final;
	args_ag.flag_terminar = &flag_terminar;

	crear_hilo_manejador_agentes(&args_ag, &hiloAgentes);

	while (hora_actual < horaF) {
		pthread_mutex_lock(&mutex_solicitudes);
		while (cola_cabeza == NULL && hora_actual < horaF)
			pthread_cond_wait(&cond_solicitudes, &mutex_solicitudes);

		// sacar solicitud si existe y procesarla (imprimir/logic)
		pthread_mutex_unlock(&mutex_solicitudes);
	}

//	flag_terminar = 1;
//	terminar = 1;
//	pthread_cond_broadcast(&cond_solicitudes);
//	pthread_join(hiloAgentes, NULL);
	pthread_detach(hiloAgentes);

	/******************************************
	 * Creación del hilo del reloj
	******************************************/

	// Configurar argumentos del reloj
	RelojArgs args;
	args.horaI = horaI;
	args.horaF = horaF;
	args.segH = segundosH;
	args.mutex = &hora_mutex;
	args.cond = &hora_cond;
	args.hora_actual = &hora_actual;
	args.terminate = &terminar;

	/* Creación el hilo reloj */
	int rc = crear_hilo_reloj(&args, &hiloReloj);
	if (rc != 0) {
		printf("Error al crear el hilo reloj\n");
		return 1;
	}

	printf("Hilo reloj inicializado en %d\n", horaI);

	// Esperar a que el reloj termine (cuando llega a horaFin)
	pthread_join(hiloReloj, NULL);

	printf("Hilo reloj finalizado. Hora final = %d\n", hora_actual);

	return 0;
}

int main(int argc, char *argv[]) {

	int horaInicio = 7;
	int horaFin = 19;
	int segundosHora = 10;
	int total = 100;
	char *pipeRecibe = "PIPE_RECEPTOR";
	char *endptr;
	//int horaInicioConfig = 0; configuración de legado *codigo viejo | recordar eliminar al final :)*
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
	/* Impresión de los detalles de la simulación */
	printf("\nInicializando la simulación con las variables ingresadas\n\n");
	printf("hora inicio: '%d'\n", horaInicio);
	printf("hora fin: '%d'\n", horaFin);
	printf("segundos en hora de simulación: '%d'\n", segundosHora);
	printf("total de personas: '%d'\n", total);
	printf("Nombre de Pipe Receptora: '%s'\n", pipeRecibe);
	/* Inicio de función principal */
	ejecutarParque(horaInicio, horaFin, segundosHora, total, pipeRecibe);
}
