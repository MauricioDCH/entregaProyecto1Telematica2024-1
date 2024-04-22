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
#define SERVER_IP "52.0.250.201" // Dirección IP del servidor proxy para uso en encabezados HTTP.


long ttl_global = 0; // Variable global para almacenar el tiempo de vida (TTL) del caché.
int port; // Puerto en el que el servidor proxy estará escuchando.
char logPath[1024]; // Ruta al archivo de log donde se registran las operaciones del servidor.
int current_server = 0; // Variable para implementar la estrategia de balanceo de carga Round Robin.

// Define una estructura para almacenar detalles relevantes de los servidores Apache configurados.
typedef struct {
    char *hostname; // Nombre de host del servidor Apache.
    char *path;
} Server;

// Inicializa un arreglo de estructuras `Server` con la configuración de los servidores Apache disponibles.
Server servers[SERVER_COUNT] = {
    {"3.219.115.240", "/"}, // Primer servidor Apache configurado.
    {"34.193.175.4", "/"}, // Segundo servidor Apache configurado.
    {"34.198.37.66", "/"}  // Tercer servidor Apache configurado.
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

void send_http_error(int client_socket, const char *status, const char *reason) {
    char response[1024];
    snprintf(response, sizeof(response), "HTTP/1.1 %s %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status, reason);
    send(client_socket, response, strlen(response), 0);
}

void *handle_request(void *client_socket_ptr) {
    /**
    * Esta función maneja las solicitudes HTTP entrantes, sirviendo respuestas desde el caché si están disponibles,
    * o redirigiéndolas a los servidores Apache configurados o a servidores externos. Implementa la lógica de balanceo de carga
    * para las solicitudes dirigidas a los servidores Apache y almacena las respuestas en el caché para uso futuro.
    *
    * @param client_socket_ptr Puntero al descriptor de socket para la conexión del cliente.
    * @return NULL siempre, conforme a la firma requerida para las funciones ejecutadas por hilos.
    */
    

    // Extrae el descriptor de socket del cliente y libera la memoria asignada para el puntero.
    int client_socket = *((int*)client_socket_ptr);
    free(client_socket_ptr); // Libera la memoria después de recuperar el valor.

    // Buffer para almacenar la solicitud entrante y la nueva solicitud a formar.
    char buffer[MAX], new_request[MAX], response_header[MAX];
    memset(buffer, 0, MAX); // Inicializa el buffer a cero.

    int bytes_received = recv(client_socket, buffer, MAX, 0); // Recibe la solicitud del cliente.

    // Variables para almacenar los componentes de la solicitud HTTP.
    char method[10], url[MAX], protocol[10];
    char *hostname = malloc(MAX * sizeof(char));
    char *path = malloc(MAX * sizeof(char));

   // Manejo de errores 
    if (bytes_received <= 0) {
        send_http_error(client_socket, "500", "Internal Server Error");
        close(client_socket);
        return NULL;
    }

    if (sscanf(buffer, "%s %s %s", method, url, protocol) < 3 || sscanf(buffer, "%s %s %s", method, url, protocol) > 3) {
        send_http_error(client_socket, "500", "Internal Server Error");
        close(client_socket);
        return NULL;
    }

    if (strncmp(method, "GET", 3) != 0 && strncmp(method, "HEAD", 4) != 0) {
        send_http_error(client_socket, "501", "Not Implemented");
        close(client_socket);
        return NULL;
    }

    char *space_ptr = strchr(buffer, ' '); // Encuentra el primer espacio para separar el método.
    strncpy(method, buffer, space_ptr - buffer); // Extrae el método de la solicitud.
    method[space_ptr - buffer] = '\0'; // Termina la cadena del método.

    // Procesa el resto de la línea de solicitud para extraer la URL y el protocolo.
    char *url_start = space_ptr + 1;
    space_ptr = strchr(url_start, ' ');
    strncpy(url, url_start, space_ptr - url_start); // Extrae la URL.
    url[space_ptr - url_start] = '\0'; // Termina la cadena de la URL.
    strcpy(protocol, space_ptr + 1); // Copia el protocolo.
    char *protocol_end = strstr(protocol, "\r\n"); // Encuentra el fin de la línea del protocolo.
    *protocol_end = '\0'; // Termina la cadena del protocolo.

    // Busca el primer '/' en la URL para separar el hostname del path.
    char *slash = strchr(url, '/');
    if (slash != NULL) {
        int hostname_length = slash - url; // Calcula la longitud del hostname.
        strncpy(hostname, url, hostname_length); // Extrae el hostname.
        hostname[hostname_length] = '\0'; // Termina la cadena del hostname.
        strcpy(path, slash); // El resto es el path.
    } else {
        strcpy(hostname, url); // Toda la URL es tratada como hostname.
        path[0] = '\0'; // No hay path.
    }

    // Registro de la solicitud recibida en el log del sistema.
    log_message("Petición recibida: %s %s %s\n", method, url, protocol);

    // Inicializa la variable para determinar si la solicitud es para un servidor Apache.
    int is_for_apache = 0;
    // Variable para almacenar el servidor seleccionado en caso de que la solicitud sea para un servidor Apache.
    Server selected_server;

    // Revisa si la URL de la solicitud coincide con alguno de los servidores Apache configurados.
    for (int i = 0; i < SERVER_COUNT; i++) {
        if (strstr(hostname, servers[i].hostname) != NULL) {
            is_for_apache = 1; // La solicitud es para un servidor Apache.
            selected_server = servers[current_server]; // Selecciona el servidor usando la estrategia Round Robin.
            selected_server.path = path;
            break;
        }
    }

    // Manejo de la caché: genera un nombre de archivo basado en la URL para la caché.
    char cache_file_name[2 * MD5_DIGEST_LENGTH + 1];
    generar_nombre_archivo_cache(method, url, cache_file_name);

    char respuesta_cache[MAX * 10]; // Buffer para la respuesta del caché.
    if (obtener_respuesta_cache(cache_file_name, respuesta_cache, ttl_global)) {
        // Si la respuesta está en caché, la envía directamente al cliente.
        log_message("Respuesta para %s servida desde caché.\n\n", url);
        send(client_socket, respuesta_cache, strlen(respuesta_cache), 0);
    } 
    else {
        // Si no está en caché, determina si debe ser manejada por un servidor Apache configurado utilizando Round Robin.
        if (is_for_apache) {
            // Prepara la solicitud para un servidor Apache configurado usando el método apropiado.
            // Formatea una nueva solicitud HTTP para enviar al servidor correspondiente.
            snprintf(new_request, MAX, "%s %s %s\r\nHost: %s\r\nVia: 1.1 %s@%s\r\nConnection: close\r\n\r\n", method, selected_server.path, protocol, selected_server.hostname, "ubuntu", SERVER_IP);
            // Avanza al siguiente servidor en la estrategia Round Robin para balancear la carga.
            hostname = selected_server.hostname;
            current_server = (current_server + 1) % SERVER_COUNT;
        }
        else {
            // Si la solicitud no es para un servidor Apache, se maneja como una solicitud externa.
            // Formatea una nueva solicitud HTTP para enviar al servidor correspondiente.
            snprintf(new_request, MAX, "%s %s %s\r\nHost: %s\r\nVia: 1.1 %s@%s\r\nConnection: close\r\n\r\n", method, path, protocol, hostname, "ubuntu", SERVER_IP);
        }

        // Preparación para conectar con el servidor destino.
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(hostname, "80", &hints, &res) != 0) {
            perror("getaddrinfo failed");
            close(client_socket);
            return NULL;
        }

        // Conexión con el servidor destino.
        int server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (connect(server_socket, res->ai_addr, res->ai_addrlen) < 0) {
            perror("Connection failed");
            send_http_error(client_socket, "500", "Internal Server Error");
            freeaddrinfo(res);
            close(client_socket);
            return NULL;
        }
        freeaddrinfo(res);

        // Envío de la solicitud al servidor destino y manejo de la respuesta.
        send(server_socket, new_request, strlen(new_request), 0);

        // Inicializa el buffer para la respuesta y lee la respuesta del servidor web.
        memset(buffer, 0, MAX);
        int bytes_received = recv(server_socket, buffer, MAX, 0);

        // Registro final de la acción realizada.
        log_message("Respuesta enviada a %s, con encabezado Via: ubuntu@%s\n", hostname, SERVER_IP);

        // Procesamiento de la respuesta del servidor.
        char *content_start = strstr(buffer, "\r\n\r\n") + 4;
        strncpy(response_header, buffer, content_start - buffer);
        response_header[content_start - buffer] = '\0';
        log_message("Encabezados de respuesta: %s", response_header);

        // Mientras haya datos para leer, sigue enviando la respuesta al cliente.
        while (bytes_received > 0) {
            send(client_socket, buffer, bytes_received, 0);
            bytes_received = recv(server_socket, buffer, MAX, 0);
        }

        almacenar_respuesta_cache(cache_file_name, buffer);

        close(server_socket);
    }

    close(client_socket);
    return NULL;
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
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> </ruta/log.log>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Ingresar por consola el tiempo de vida del caché.
    printf("Ingrese el tiempo de vida de los archivos de caché (TTL) en segundos: ");
    scanf("%ld", &ttl_global);
    printf("Usted ingresó \'%ld\' segundos de tiempo de vida para los archivos caché.\n", ttl_global);
    
    port = atoi(argv[1]); // Puerto en el cual el servidor debe escuchar.
    strncpy(logPath, argv[2], sizeof(logPath) - 1); // Ruta al archivo de log.

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