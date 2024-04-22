#ifndef MANEJOCACHE_H
#define MANEJOCACHE_H

#define CACHE_SIZE (MAXIMUM_SIZE_RESPONSE*2)  // Establece un tamaño de caché considerable para almacenar respuestas HTTP.
#define CACHE_DIR "./cache/historial"  // Define el directorio actual como ubicación del caché.

void generar_nombre_archivo_cache(const char *url, char *nombre_archivo_cache);
void almacenar_respuesta_cache(const char *nombre_archivo_cache, const char *request, const char *respuesta);
int obtener_respuesta_cache(const char *nombre_archivo_cache, char *respuesta);
void limpiar_cache(const char *directorio_cache);

#endif
