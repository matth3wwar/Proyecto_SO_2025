/***********************************************************
* Pontificia Universidad Javeriana
* Autores: Mateo David Guerra y Ángel Daniel García Santana
* Fecha: Noviembre 2025
* Materia: Sistemas Operativos
* Proyecto: Sistema de Reservas - Controlador
* Tema: Uso de Named Pipes, Threads y programación concurrente
************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define MAX_AGENTES 50
#define MAX_RESERVAS 1000
#define MAX_FAMILIA 50
#define MAX_AGENTE 50
#define BUFFER_SIZE 512

// Estructuras de datos
typedef struct Agente {
	char nombre[MAX_AGENTE];
	char pipe_respuesta[100];
	struct Agente *siguiente;
} Agente;

typedef struct Reserva {
	char familia[MAX_FAMILIA];
	char agente[MAX_AGENTE];
	int hora_entrada;
	int num_personas;
	// 1: aceptada, 2: reprogramada, 3: rechazada
	int estado;
	struct Reserva *siguiente;
} Reserva;

typedef struct EstadoHora {
	int capacidad_actual;
	int capacidad_maxima;
	int personas_entrando;
	int personas_saliendo;
	Reserva *reservas_activas;
} EstadoHora;

typedef struct MensajeAgente {
	char tipo[20];
	char nombre_agente[MAX_AGENTE];
	char pipe_respuesta[100];
	char familia[MAX_FAMILIA];
	int hora_solicitada;
	int num_personas;
} MensajeAgente;

/* Variables globales */
EstadoHora estado_horas[24];
Agente *lista_agentes = NULL;
Reserva *lista_reservas = NULL;

pthread_mutex_t mutex_agentes = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_reservas = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_horas = PTHREAD_MUTEX_INITIALIZER;

volatile int running = 1;
volatile int hora_actual = 7;
int hora_inicio = 7;
int hora_fin = 19;
int segundos_por_hora = 10;
int capacidad_maxima = 100;
char pipe_controlador[100] = "/tmp/pipe_controlador";

/* Estadísticas para reporte final */
int solicitudes_aceptadas = 0;
int solicitudes_reprogramadas = 0;
int solicitudes_rechazadas = 0;

/* Prototipos de funciones */
void manejar_senal(int sig);
void inicializar_sistema();
void limpiar_sistema();
void *hilo_receptor_agentes(void *arg);
void *hilo_reloj_simulacion(void *arg);
void procesar_mensaje_agente(MensajeAgente *mensaje);
void registrar_agente(MensajeAgente *mensaje);
void procesar_solicitud_reserva(MensajeAgente *mensaje);
int verificar_disponibilidad(int hora_inicio, int num_personas);
int encontrar_hora_alternativa(int hora_solicitada, int num_personas);
void responder_agente(const char *pipe_respuesta, const char *mensaje);
void avanzar_hora_simulacion();
void generar_reporte_final();
void agregar_reserva(const char *familia, const char *agente, int hora_entrada, int num_personas, int estado);

/* Manejar señal de terminación */
void manejar_senal(int sig) {
	printf("\n=====| SEÑAL DE TERMINACIÓN RECIBIDA |=====\n");
	running = 0;
	generar_reporte_final();
	limpiar_sistema();
	exit(0);
}

/* Inicializar sistema */
void inicializar_sistema() {
	printf("\nInicializando el sistema...\n");

	for (int i = 0; i < 24; i++) {
		estado_horas[i].capacidad_actual = 0;
		estado_horas[i].capacidad_maxima = capacidad_maxima;
		estado_horas[i].personas_entrando = 0;
		estado_horas[i].personas_saliendo = 0;
		estado_horas[i].reservas_activas = NULL;
	}

	// Crear pipe del controlador
	if (mkfifo(pipe_controlador, 0666) == -1 && errno != EEXIST) {
		perror("Error creando pipe del controlador");
		exit(1);
	}

	printf("Pipe del controlador creado: %s\n", pipe_controlador);
}

// Limpiar sistema (se ejecuta con la finalización del código para no ocupar recursos adicionales)
void limpiar_sistema() {
	printf("Limpiando recursos del sistema...\n");

	// Limpiar lista de agentes
	Agente *agente_actual = lista_agentes;
	while (agente_actual != NULL) {
		Agente *temp = agente_actual;
	agente_actual = agente_actual->siguiente;
	free(temp);
	}

	// Limpiar lista de reservas
	Reserva *reserva_actual = lista_reservas;
	while (reserva_actual != NULL) {
		Reserva *temp = reserva_actual;
		reserva_actual = reserva_actual->siguiente;
		free(temp);
	}

	// Eliminar pipe
	unlink(pipe_controlador);
}

/* Hilo receptor de agentes */
void *hilo_receptor_agentes(void *arg) {
	printf("Hilo receptor de agentes iniciado\n");

	while (running) {
		int fd = open(pipe_controlador, O_RDONLY);
		if (fd == -1) {
			if (running) {
				perror("Error abriendo pipe del controlador");
			}
			break;
		}

		MensajeAgente mensaje;
		ssize_t bytes_leidos = read(fd, &mensaje, sizeof(mensaje));
		close(fd);

		if (bytes_leidos == sizeof(mensaje)) {
			procesar_mensaje_agente(&mensaje);
		} else if (bytes_leidos > 0) {
			fprintf(stderr, "Error: Mensaje de tamaño incorrecto\n");
		}
	}

	printf("Hilo receptor de agentes terminado\n");
	return NULL;
}

/* Hilo del reloj de simulación */
void *hilo_reloj_simulacion(void *arg) {
	printf("Hilo del reloj de simulación iniciado\n");
	printf("Hora inicial: %d, Hora final: %d, Segundos por hora: %d\n", hora_inicio, hora_fin, segundos_por_hora);

	while (running && hora_actual <= hora_fin) {
		sleep(segundos_por_hora);

		if (!running) break;

		pthread_mutex_lock(&mutex_horas);
		avanzar_hora_simulacion();
		pthread_mutex_unlock(&mutex_horas);
	}

	printf("Hilo del reloj de simulación terminado\n");

	// Generar reporte final cuando termina la simulación
	if (hora_actual > hora_fin) {
		generar_reporte_final();
		running = 0;
	}

	return NULL;
}

// Procesar mensaje del agente
void procesar_mensaje_agente(MensajeAgente *mensaje) {
	printf("Mensaje recibido - Tipo: %s, Agente: %s\n", mensaje->tipo, mensaje->nombre_agente);

	if (strcmp(mensaje->tipo, "REGISTRO") == 0) {
		registrar_agente(mensaje);
	} else if (strcmp(mensaje->tipo, "RESERVA") == 0) {
		procesar_solicitud_reserva(mensaje);
	} else {
		fprintf(stderr, "Error: Tipo de mensaje desconocido: %s\n", mensaje->tipo);
	}
}

// Registrar nuevo agente
void registrar_agente(MensajeAgente *mensaje) {
	pthread_mutex_lock(&mutex_agentes);

	// Agregar agente a la lista
	Agente *nuevo_agente = malloc(sizeof(Agente));
	strncpy(nuevo_agente->nombre, mensaje->nombre_agente, sizeof(nuevo_agente->nombre));
	strncpy(nuevo_agente->pipe_respuesta, mensaje->pipe_respuesta, sizeof(nuevo_agente->pipe_respuesta));
	nuevo_agente->siguiente = lista_agentes;
	lista_agentes = nuevo_agente;

	pthread_mutex_unlock(&mutex_agentes);

	// Responder con hora actual
	char respuesta[BUFFER_SIZE];
	snprintf(respuesta, sizeof(respuesta), "%d", hora_actual);
	responder_agente(mensaje->pipe_respuesta, respuesta);

	printf("NUEVO AGENTE REGISTRADO: %s (Pipe: %s)\n", mensaje->nombre_agente, mensaje->pipe_respuesta);
}

// Procesar solicitud de reserva
void procesar_solicitud_reserva(MensajeAgente *mensaje) {
	printf("SOLICITUD RECIBIDA: Agente %s - Familia %s, Hora %d, Personas %d\n", mensaje->nombre_agente, mensaje->familia, mensaje->hora_solicitada, mensaje->num_personas);

	char respuesta[BUFFER_SIZE];

	// *VALIDACIÓN 1: Hora fuera del rango de simulación*
	if (mensaje->hora_solicitada > hora_fin) {
		snprintf(respuesta, sizeof(respuesta),
		"RESERVA NEGADA: Familia %s - Hora solicitada (%d) fuera del horario del parque (hora fin: %d)",
                 mensaje->familia, mensaje->hora_solicitada, hora_fin);
		solicitudes_rechazadas++;
	}
	// *VALIDACIÓN 2: Número de personas excede capacidad máxima*
	else if (mensaje->num_personas > capacidad_maxima) {
		snprintf(respuesta, sizeof(respuesta),
		"RESERVA NEGADA: Familia %s - Número de personas (%d) excede el aforo máximo (%d)",
		mensaje->familia, mensaje->num_personas, capacidad_maxima);
		solicitudes_rechazadas++;
	}
	// *VALIDACIÓN 3: Hora ya pasó*
	else if (mensaje->hora_solicitada < hora_actual) {
		snprintf(respuesta, sizeof(respuesta),
		"RESERVA NEGADA POR EXTEMPORÁNEA: Familia %s - Hora solicitada (%d) ya pasó (hora actual: %d)",
		mensaje->familia, mensaje->hora_solicitada, hora_actual);
		solicitudes_rechazadas++;

		// Buscar alternativa para reserva extemporánea
		int hora_alternativa = encontrar_hora_alternativa(mensaje->hora_solicitada, mensaje->num_personas);
		if (hora_alternativa != -1) {
			snprintf(respuesta, sizeof(respuesta),
			"RESERVA REPROGRAMADA: Familia %s - Aceptada para hora %d (solicitó %d) con %d personas",
			mensaje->familia, hora_alternativa, mensaje->hora_solicitada, mensaje->num_personas);
			agregar_reserva(mensaje->familia, mensaje->nombre_agente, hora_alternativa, mensaje->num_personas, 2);
			solicitudes_reprogramadas++;
		}
	}
	// *VERIFICAR DISPONIBILIDAD PARA HORA SOLICITADA*
	else {
		int disponible = verificar_disponibilidad(mensaje->hora_solicitada, mensaje->num_personas);

		if (disponible) {
			// *RESERVA ACEPTADA EN HORA SOLICITADA*
			snprintf(respuesta, sizeof(respuesta),
			"RESERVA OK: Familia %s - Aceptada para hora %d con %d personas",
			mensaje->familia, mensaje->hora_solicitada, mensaje->num_personas);

			agregar_reserva(mensaje->familia, mensaje->nombre_agente, mensaje->hora_solicitada, mensaje->num_personas, 1);
			solicitudes_aceptadas++;
		} else {
			// *BUSCAR HORA ALTERNATIVA*
			int hora_alternativa = encontrar_hora_alternativa(mensaje->hora_solicitada, mensaje->num_personas);

			if (hora_alternativa != -1) {
				// *RESERVA REPROGRAMADA*
				snprintf(respuesta, sizeof(respuesta),
				"RESERVA REPROGRAMADA: Familia %s - Aceptada para hora %d (solicitó %d) con %d personas",
				mensaje->familia, hora_alternativa, mensaje->hora_solicitada, mensaje->num_personas);

				agregar_reserva(mensaje->familia, mensaje->nombre_agente, hora_alternativa, mensaje->num_personas, 2);
				solicitudes_reprogramadas++;
			} else {
				// *RESERVA NEGADA SIN ALTERNATIVAS*
				snprintf(respuesta, sizeof(respuesta), "RESERVA NEGADA: Familia %s - No hay cupo disponible para ningún horario", mensaje->familia);
				solicitudes_rechazadas++;
			}
		}
	}

	// Enviar respuesta al agente
	responder_agente(mensaje->pipe_respuesta, respuesta);
	printf("RESPUESTA ENVIADA: %s\n", respuesta);
}

// Verificar disponibilidad para 2 horas consecutivas
int verificar_disponibilidad(int hora_inicio, int num_personas) {
	pthread_mutex_lock(&mutex_horas);

	// Verificar que hay cupo para las 2 horas
	for (int h = hora_inicio; h < hora_inicio + 2 && h <= hora_fin; h++) {
		if (estado_horas[h].capacidad_actual + num_personas > estado_horas[h].capacidad_maxima) {
			pthread_mutex_unlock(&mutex_horas);
			return 0; // No hay cupo
		}
	}

	// Reservar el cupo
	for (int h = hora_inicio; h < hora_inicio + 2 && h <= hora_fin; h++) {
		estado_horas[h].capacidad_actual += num_personas;
		if (h == hora_inicio) {
			estado_horas[h].personas_entrando += num_personas;
		}
		if (h == hora_inicio + 1) {
			estado_horas[h].personas_saliendo += num_personas;
		}
	}

	pthread_mutex_unlock(&mutex_horas);
	return 1; // Cupo disponible
}

// Encontrar hora alternativa disponible
int encontrar_hora_alternativa(int hora_solicitada, int num_personas) {
	pthread_mutex_lock(&mutex_horas);

	// Buscar cualquier bloque de 2 horas disponible
	for (int h = hora_actual; h <= hora_fin - 1; h++) {
		int disponible = 1;

		// Verificar las 2 horas
		for (int h2 = h; h2 < h + 2 && h2 <= hora_fin; h2++) {
			if (estado_horas[h2].capacidad_actual + num_personas > estado_horas[h2].capacidad_maxima) {
				disponible = 0;
				break;
			}
		}

		if (disponible) {
			// Reservar el cupo
			for (int h2 = h; h2 < h + 2 && h2 <= hora_fin; h2++) {
				estado_horas[h2].capacidad_actual += num_personas;
				if (h2 == h) {
				estado_horas[h2].personas_entrando += num_personas;
				}
				if (h2 == h + 1) {
					estado_horas[h2].personas_saliendo += num_personas;
				}
			}

			pthread_mutex_unlock(&mutex_horas);
			return h;
		}
	}

	pthread_mutex_unlock(&mutex_horas);
	return -1; // No hay alternativas
}

// Responder al agente
void responder_agente(const char *pipe_respuesta, const char *mensaje) {
	int fd = open(pipe_respuesta, O_WRONLY);
	if (fd == -1) {
		perror("Error abriendo pipe de respuesta del agente");
		return;
	}

	write(fd, mensaje, strlen(mensaje) + 1);
	close(fd);
}

// Avanzar hora de simulación
void avanzar_hora_simulacion() {
	hora_actual++;

	printf("\n=====| HORA ACTUAL: %d |=====\n", hora_actual);
	printf("\nPersonas entrando: %d\n", estado_horas[hora_actual].personas_entrando);
	printf("Personas saliendo: %d\n", estado_horas[hora_actual].personas_saliendo);
	printf("Personas presentes: %d\n", estado_horas[hora_actual].capacidad_actual);

	// Resetear contadores de movimiento para la próxima hora
	if (hora_actual + 1 < 24) {
		estado_horas[hora_actual + 1].personas_entrando = 0;
		estado_horas[hora_actual + 1].personas_saliendo = 0;
	}

	// Verificar fin de simulación
	if (hora_actual >= hora_fin) {
		printf("=====| FINAL DE LA SIMULACIÓN |=====\n");
	}
}

/* Agregar reserva a la lista */
void agregar_reserva(const char *familia, const char *agente, int hora_entrada, int num_personas, int estado) {
	pthread_mutex_lock(&mutex_reservas);

	Reserva *nueva_reserva = malloc(sizeof(Reserva));
	strncpy(nueva_reserva->familia, familia, sizeof(nueva_reserva->familia));
	strncpy(nueva_reserva->agente, agente, sizeof(nueva_reserva->agente));
	nueva_reserva->hora_entrada = hora_entrada;
	nueva_reserva->num_personas = num_personas;
	nueva_reserva->estado = estado;
	nueva_reserva->siguiente = lista_reservas;
	lista_reservas = nueva_reserva;

	pthread_mutex_unlock(&mutex_reservas);
}

/* Generar reporte final */
void generar_reporte_final() {
	printf("\n=====| REPORTE FINAL DEL SISTEMA DE RESERVAS |=====\n");

	// Calcular horas pico y horas bajas
	int max_personas = 0;
	int min_personas = capacidad_maxima;
	int horas_pico[24], num_horas_pico = 0;
	int horas_bajas[24], num_horas_bajas = 0;

	for (int i = hora_inicio; i <= hora_fin; i++) {
		if (estado_horas[i].capacidad_actual > max_personas) {
			max_personas = estado_horas[i].capacidad_actual;
			num_horas_pico = 0;
			horas_pico[num_horas_pico++] = i;
		} else if (estado_horas[i].capacidad_actual == max_personas) {
			horas_pico[num_horas_pico++] = i;
		}

		if (estado_horas[i].capacidad_actual < min_personas) {
			min_personas = estado_horas[i].capacidad_actual;
			num_horas_bajas = 0;
			horas_bajas[num_horas_bajas++] = i;
		} else if (estado_horas[i].capacidad_actual == min_personas) {
			horas_bajas[num_horas_bajas++] = i;
		}
	}

	/* Imprimir estadísticas */
	printf("\n=====| ESTADÍSTICAS DE SOLICITUDES |=====\n\n");
	printf("Solicitudes aceptadas en hora solicitada: %d\n", solicitudes_aceptadas);
	printf("Solicitudes reprogramadas: %d\n", solicitudes_reprogramadas);
	printf("Solicitudes rechazadas: %d\n", solicitudes_rechazadas);
	printf("Total de solicitudes procesadas: %d\n", solicitudes_aceptadas + solicitudes_reprogramadas + solicitudes_rechazadas);

	printf("\n=====| ANÁLISIS DE OCUPACIÓN |=====\n\n");
	printf("Horas pico (%d personas): ", max_personas);
	for (int i = 0; i < num_horas_pico; i++) {
		printf("%d ", horas_pico[i]);
	}
	printf("\n");

	printf("Horas de menor afluencia (%d personas): ", min_personas);
	for (int i = 0; i < num_horas_bajas; i++) {
		printf("%d ", horas_bajas[i]);
	}
	printf("\n");

	printf("\n=====| RESUMEN POR HORA |=====\n\n");
	for (int i = hora_inicio; i <= hora_fin; i++) {
		printf("Hora %d: %d personas (de %d máximo)\n", i, estado_horas[i].capacidad_actual, capacidad_maxima);
	}

	printf("\n=====| FIN DEL REPORTE |=====\n");
}

int main(int argc, char *argv[]) {
	// Configurar manejador de señales
	signal(SIGINT, manejar_senal);
	signal(SIGTERM, manejar_senal);

	// Valores por defecto
	hora_inicio = 7;
	hora_fin = 19;
	segundos_por_hora = 10;
	capacidad_maxima = 100;
	strcpy(pipe_controlador, "/tmp/pipe_controlador");

	// Parseo de argumentos
	int i = 1;
	while (i < argc) {
		if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
			hora_inicio = atoi(argv[i + 1]);
			i += 2;
		} else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			hora_fin = atoi(argv[i + 1]);
			i += 2;
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			segundos_por_hora = atoi(argv[i + 1]);
			i += 2;
		} else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			capacidad_maxima = atoi(argv[i + 1]);
			i += 2;
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			strncpy(pipe_controlador, argv[i + 1], sizeof(pipe_controlador) - 1);
			i += 2;
		} else {
			fprintf(stderr, "Uso: %s -i hora_inicio -f hora_fin -s segundos_por_hora -t capacidad_maxima -p pipe_controlador\n", argv[0]);
			fprintf(stderr, "Ejemplo: %s -i 7 -f 19 -s 10 -t 100 -p /tmp/pipe_controlador\n", argv[0]);
			return 1;
		}
	}

	// Validación de parámetros
	if (hora_inicio < 1 || hora_inicio > 24 || hora_fin < 1 || hora_fin > 24 || hora_fin <= hora_inicio) {
		fprintf(stderr, "Error: Horas inválidas. Deben estar entre 1-24 y hora_fin > hora_inicio\n");
		return 1;
	}

	if (segundos_por_hora <= 0 || capacidad_maxima <= 0) {
		fprintf(stderr, "Error: segundos_por_hora y capacidad_maxima deben ser positivos\n");
		return 1;
	}

	// Mostrar configuración
	printf("=====| INICIANDO CONTROLADOR |=====\n");
	printf("Hora inicio: %d\n", hora_inicio);
	printf("Hora fin: %d\n", hora_fin);
	printf("Segundos por hora de simulación: %d\n", segundos_por_hora);
	printf("Capacidad máxima por hora: %d\n", capacidad_maxima);
	printf("Pipe del controlador: %s\n", pipe_controlador);

	// Inicializar sistema
	hora_actual = hora_inicio;
	inicializar_sistema();

	// Crear hilos
	pthread_t hilo_receptor, hilo_reloj;

	if (pthread_create(&hilo_receptor, NULL, hilo_receptor_agentes, NULL) != 0) {
		perror("Error creando hilo receptor de agentes");
		limpiar_sistema();
		return 1;
	}

	if (pthread_create(&hilo_reloj, NULL, hilo_reloj_simulacion, NULL) != 0) {
		perror("Error creando hilo del reloj");
		running = 0;
		pthread_join(hilo_receptor, NULL);
		limpiar_sistema();
		return 1;
	}

	printf("Sistema inicializado correctamente. Esperando agentes...\n");

	// Esperar a que terminen los hilos
	pthread_join(hilo_reloj, NULL);
	running = 0;
	pthread_join(hilo_receptor, NULL);

	// Limpieza final
	limpiar_sistema();
	printf("Controlador terminado correctamente.\n");

	return 0;
}
