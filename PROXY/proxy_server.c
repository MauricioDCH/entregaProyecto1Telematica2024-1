#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include "manejoCache.h"

#define MAX 4000 // Define el tamaño máximo de los buffers utilizados.
#define SERVER_COUNT 3 // Especifica el número de servidores web Apache configurados.
#define SERVER_IP "44.202.61.119" // Dirección IP del servidor proxy para uso en encabezados HTTP.

long ttl_global = 0; // Variable global para almacenar el tiempo de vida (TTL) del caché.
int port; // Puerto en el que el servidor proxy estará escuchando.
char logPath[1024]; // Ruta al archivo de log donde se registran las operaciones del servidor.
int current_server = 0; // Variable para implementar la estrategia de balanceo de carga Round Robin.

// Define una estructura para almacenar detalles relevantes de los servidores Apache configurados.
typedef struct {
    char *hostname; // Nombre de host del servidor Apache.
    char *documentRoot; // Ruta al directorio raíz de documentos del servidor.
} Server;

// Inicializa un arreglo de estructuras `Server` con la configuración de los servidores Apache disponibles.
Server servers[SERVER_COUNT] = {
    {"page1.com", "/var/www/page1"}, // Primer servidor Apache configurado.
    {"page2.com", "/var/www/page2"}, // Segundo servidor Apache configurado.
    {"page3.com", "/var/www/page3"}  // Tercer servidor Apache configurado.
};


void log_message(const char *format, ...) {
    /**
    * Escribe un mensaje formatado tanto en la salida estándar como en un archivo de log.
    * 
    * @param format Cadena de formato que sigue la misma especificación de la familia de funciones printf.
    * @param ... Argumentos variables que se utilizarán para reemplazar los especificadores de formato.
    *
    * Esta función es una utilidad de logging que facilita la escritura simultánea en la salida estándar y
    * en un archivo de log. Es flexible y puede ser utilizada con cualquier número y tipo de argumentos
    * al estilo de printf.
    */
    
    va_list args; // Inicializa la lista de argumentos variables.
    
    // Intenta abrir el archivo de log en modo append. Si el archivo no existe, se crea.
    FILE *log_file = fopen(logPath, "a");
    // Verifica si hubo un error al abrir el archivo.
    if (log_file == NULL) {
        // Si no se puede abrir el archivo de log, imprime el error y sale del programa.
        perror("Error abriendo el archivo de log");
        exit(EXIT_FAILURE);
    }
    
    // Inicializa args para recuperar los argumentos adicionales después de format.
    va_start(args, format);
    // Imprime el mensaje en la salida estándar usando la lista de argumentos variables.
    vprintf(format, args);
    // Finaliza la recuperación de los argumentos variables.
    va_end(args);
    
    // Reinicia args para el nuevo uso con vfprintf.
    va_start(args, format);
    // Escribe el mensaje en el archivo de log usando la lista de argumentos variables.
    vfprintf(log_file, format, args);
    // Finaliza la recuperación de los argumentos variables.
    va_end(args);
    
    // Cierra el archivo de log para asegurar que el mensaje se escriba en el disco.
    fclose(log_file);
}

void *handle_request(void *client_socket_ptr) {
    /**
    * Esta función maneja las solicitudes HTTP entrantes, sirviendo respuestas desde el caché si están disponibles,
    * o redirigiéndolas a los servidores Apache configurados o a servidores externos. Implementa la lógica de balanceo de carga
    * para las solicitudes dirigidas a los servidores Apache y almacena las respuestas en el caché para uso futuro.
    *
    * @param client_socket_ptr Puntero al descriptor de socket para la conexión del cliente.
    * @return NULL siempre, conforme a la firma requerida para las funciones ejecutadas por hilos.
    *
    * La función comienza extrayendo el descriptor del socket del cliente del puntero proporcionado y luego libera
    * el puntero. Lee la solicitud HTTP del cliente y extrae el método, la URL y el protocolo. Utiliza esta información
    * para determinar si la solicitud se puede servir desde el caché. Si es así, envía la respuesta almacenada en el caché
    * directamente al cliente. Si no, determina si la solicitud debe ser manejada por uno de los servidores Apache configurados
    * o si debe ser redirigida a un servidor externo. Construye una nueva solicitud HTTP según sea necesario y maneja la conexión
    * al servidor correspondiente. Finalmente, almacena las nuevas respuestas en el caché para futuras solicitudes.
    */

    // Extrae el descriptor de socket del cliente y libera la memoria asignada para el puntero.
    int client_socket = *((int*)client_socket_ptr);
    free(client_socket_ptr); // Libera la memoria asignada para el descriptor del socket

    char buffer[MAX], new_request[MAX];
    // Prepara los buffers para leer la solicitud y almacenar la nueva solicitud a enviar.
    memset(buffer, 0, MAX);
    // Recibe la solicitud del cliente.
    recv(client_socket, buffer, MAX, 0);

    // Variables para almacenar los componentes de la solicitud HTTP.
    char method[10], url[MAX], protocol[10];
    // Extrae el método, la URL y el protocolo de la solicitud.
    sscanf(buffer, "%s %s %s", method, url, protocol);

    // Registra la recepción de la solicitud.
    log_message("Petición recibida: %s %s %s\n", method, url, protocol);

    // Inicializa la variable para determinar si la solicitud es para un servidor Apache.
    int is_for_apache = 0;
    // Variable para almacenar el servidor seleccionado en caso de que la solicitud sea para un servidor Apache.
    Server selected_server;

    // Revisa si la URL de la solicitud coincide con alguno de los servidores Apache configurados.
    for (int i = 0; i < SERVER_COUNT; i++) {
        if (strstr(url, servers[i].hostname) != NULL) {
            is_for_apache = 1; // La solicitud es para un servidor Apache.
            selected_server = servers[current_server]; // Selecciona el servidor usando la estrategia Round Robin.
            break;
        }
    }

    // Genera el nombre del archivo de caché basado en la URL.
    char nombre_archivo_cache[2 * MD5_DIGEST_LENGTH + 1] = {0};
    generar_nombre_archivo_cache(url, nombre_archivo_cache);

    char respuesta_cache[MAX * 10]; // Buffer para almacenar la respuesta potencialmente recuperada del caché.
    // Intenta recuperar la respuesta del caché.
    if (obtener_respuesta_cache(nombre_archivo_cache, respuesta_cache, ttl_global)) {
        // Si existe una respuesta en el caché, la envía al cliente.
        log_message("Respuesta para %s servida desde caché.\n\n", url);
        send(client_socket, respuesta_cache, strlen(respuesta_cache), 0);
    } else {
        // Si la respuesta no está en el caché, procede a construir y enviar una nueva solicitud.
        // Encuentra el encabezado "Host:" en la solicitud original para determinar el destino.
        char *hostStart = strstr(buffer, "Host: ");
        if (!hostStart) {
            // En caso de no encontrar el encabezado "Host:", envía una respuesta de error al cliente.
            const char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            send(client_socket, bad_request, strlen(bad_request), 0);
            close(client_socket);
            return;
        }

        // Extrae el nombre del host de la solicitud.
        hostStart += strlen("Host: ");
        char *hostEnd = strstr(hostStart, "\r\n");
        char hostname[MAX];
        ptrdiff_t hostnameLen = hostEnd - hostStart;
        strncpy(hostname, hostStart, hostnameLen);
        hostname[hostnameLen] = '\0';

        // Construye la nueva solicitud HTTP según si es para un servidor Apache o externo.
        // Esta sección redirige la solicitud apropiadamente y maneja la respuesta.

        // Determina si la solicitud es para un servidor Apache configurado.
        if (is_for_apache) {
            // Prepara la solicitud para un servidor Apache configurado usando el método apropiado.
            if (strcmp(method, "HEAD") == 0) {
                // Construye una solicitud HEAD con el encabezado "Via" para identificar el proxy.
                snprintf(new_request, MAX, "HEAD / HTTP/1.1\r\nHost: %s\r\nVia: 1.1 ubuntu@%s\r\nConnection: close\r\n\r\n", selected_server.hostname, SERVER_IP);
            } else {
                // Construye una solicitud GET con el encabezado "Via" para identificar el proxy.
                snprintf(new_request, MAX, "GET / HTTP/1.1\r\nHost: %s\r\nVia: 1.1 ubuntu@%s\r\nConnection: close\r\n\r\n", selected_server.hostname, SERVER_IP);
            // Aquí, conecta y envía new_request al servidor Apache seleccionado...
            }
            // Avanza al siguiente servidor en la estrategia Round Robin para balancear la carga.
            current_server = (current_server + 1) % SERVER_COUNT;
        } else {
            // Si la solicitud no es para un servidor Apache, se maneja como una solicitud externa.
            if (strcmp(method, "HEAD") == 0) {
                // Construye una solicitud HEAD para el servidor externo, incluyendo el encabezado "Via".
                snprintf(new_request, MAX, "HEAD %s HTTP/1.1\r\nHost: %s\r\nVia: 1.1 ubuntu@%s\r\nConnection: close\r\n\r\n", url, hostname, SERVER_IP);
            } else {
                // Construye la solicitud (GET, POST, etc.) para el servidor externo, incluyendo el encabezado "Via".
                snprintf(new_request, MAX, "%s %s %s\r\nHost: %s\r\nVia: 1.1 ubuntu@%s\r\nConnection: close\r\n\r\n", method, url, protocol, hostname, SERVER_IP);
            }
        }

        // Prepara la estructura de dirección para la conexión de red.
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; // Utiliza IPv4.
        hints.ai_socktype = SOCK_STREAM; // Especifica un socket de flujo.

        // Intenta resolver la dirección del servidor web (Apache o externo).
        if (getaddrinfo(hostname, "80", &hints, &res) != 0) {
            perror("getaddrinfo failed");
            close(client_socket);
            return;
        }

        // Crea un socket para la conexión con el servidor web.
        int server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        // Intenta conectar con el servidor web.
        if (connect(server_socket, res->ai_addr, res->ai_addrlen) < 0) {
            perror("Connection failed");
            freeaddrinfo(res); // Libera la memoria de los resultados de getaddrinfo.
            close(client_socket);
            return;
        }
        freeaddrinfo(res); // Libera la memoria de los resultados de getaddrinfo.

        // Envía la nueva solicitud al servidor web.
        send(server_socket, new_request, strlen(new_request), 0);

        // Inicializa el buffer para la respuesta y lee la respuesta del servidor web.
        memset(buffer, 0, MAX);
        int bytes_received = recv(server_socket, buffer, MAX, 0);
        // Mientras haya datos para leer, sigue enviando la respuesta al cliente.
        while (bytes_received > 0) {
            send(client_socket, buffer, bytes_received, 0);
            // Aquí deberías concatenar `buffer` a `respuesta_cache` si esperas respuestas más grandes que MAX
            bytes_received = recv(server_socket, buffer, MAX, 0);
        }

        // Una vez terminado el envío de la respuesta, almacena la respuesta en caché para uso futuro.
        almacenar_respuesta_cache(nombre_archivo_cache, buffer);

        // Después de manejar la solicitud y enviar toda la respuesta al cliente, registra la acción.
        log_message("Respuesta enviada a %s, con encabezado Via: ubuntu@%s\n\n", hostname, SERVER_IP);

        // Cierra el socket del servidor web.
        close(server_socket);
    }

    // Cierra la conexión con el cliente y finaliza el hilo.
    close(client_socket);
    return NULL; // Finaliza el hilo retornando NULL
}

int main(int argc, char *argv[]) {
    /**
    * La función principal del servidor proxy. Inicializa el servidor, crea un hilo para la limpieza del caché,
    * y entra en un bucle infinito para aceptar y manejar solicitudes de clientes en hilos separados.
    *
    * @param argc Número de argumentos de línea de comando.
    * @param argv Array de argumentos de línea de comando.
    * @return int Retorna 0 en una terminación exitosa, aunque en práctica este servidor está diseñado para correr indefinidamente.
    *
    * La función verifica que se hayan proporcionado los argumentos correctos en la línea de comando,
    * establece el tiempo de vida (TTL) del caché, el puerto en el que el servidor debe escuchar, y la ruta del archivo de log.
    * Luego, inicia un hilo para realizar la limpieza periódica del caché basada en el TTL especificado.
    * Finalmente, crea un socket para escuchar las solicitudes entrantes, acepta estas solicitudes y las maneja
    * creando un nuevo hilo para cada una, permitiendo así el procesamiento concurrente de múltiples solicitudes.
    */

    // Verifica que se hayan proporcionado todos los argumentos necesarios.
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <TTL> <puerto> </ruta/log.log>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Establece las variables globales basadas en los argumentos de entrada.
    ttl_global = atol(argv[1]); // Tiempo de vida del caché en segundos.
    port = atoi(argv[2]); // Puerto en el cual el servidor debe escuchar.
    strncpy(logPath, argv[3], sizeof(logPath) - 1); // Ruta al archivo de log.

    // Inicializa un hilo de limpieza del caché.
    pthread_t hilo_de_limpieza;
    // Inicializa los argumentos para la función de limpieza del caché
    struct LimpiarArgs limpiarArgs;
    limpiarArgs.ttl = ttl_global; // Establece el valor del TTL
    if (pthread_create(&hilo_de_limpieza, NULL, funcion_limpiar, (void *)&limpiarArgs) != 0) {
        perror("No se pudo crear el hilo de limpieza del caché");
        exit(EXIT_FAILURE);
    }
    pthread_detach(hilo_de_limpieza); // Asegura que los recursos del hilo se liberen automáticamente al terminar.

    // Crea el socket del servidor.
    int sockfd, *client_socket_ptr;
    struct sockaddr_in servaddr, cli;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configura la dirección y puerto del servidor.
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    // Vincula el socket a la dirección y puerto configurados.
    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
        perror("socket bind failed");
        exit(EXIT_FAILURE);
    }

    // Pone el servidor en modo escucha, listo para aceptar conexiones.
    if (listen(sockfd, 5) != 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);

    // Bucle infinito para aceptar y manejar solicitudes de clientes.
    while (1) {
        client_socket_ptr = malloc(sizeof(int));
        if (client_socket_ptr == NULL) {
            perror("Failed to allocate memory for client socket pointer");
            continue;
        }

        // Acepta una conexión de cliente.
        socklen_t len = sizeof(cli);
        *client_socket_ptr = accept(sockfd, (struct sockaddr *)&cli, &len);
        if (*client_socket_ptr < 0) {
            perror("server accept failed");
            free(client_socket_ptr);
            continue;
        }

        // Crea un nuevo hilo para manejar la solicitud del cliente.
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_request, (void*)client_socket_ptr) != 0) {
            perror("Failed to create thread for client request");
            close(*client_socket_ptr);
            free(client_socket_ptr);
        } else {
            pthread_detach(tid); // Desacopla el hilo para evitar la necesidad de unirse explícitamente más tarde.
        }
    }

    // Cierra el socket del servidor al terminar, aunque este punto no se alcanza en práctica.
    close(sockfd);
    return 0;
}
