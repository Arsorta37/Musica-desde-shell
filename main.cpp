#include <iostream>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <thread>

#define MINIAUDIO_IMPLEMENTATION
#include "reproductor.hpp"

const std::string FICHERO_OPCIONES = "opciones.txt";

struct Album {
    std::string nombre;
    unsigned num_canciones;
};

// Muestra por pantalla los albums de la carpeta destino junto con su cantidad de canciones,
// y devuelve el número de albums de la carpeta. En caso de error, devuelve 0.
unsigned mostrarAlbumsEnCarpeta(const std::string& r) {
    // Buscamos los archivos
    unsigned contador = 0;
    std::vector<Album> albums;
    albums.clear();
    std::cout << "\033[2K\r" << "Buscando albums en " << color_Carpeta << r << RESET << "... ";
    try {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(r)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                if (path.ends_with(".mp3") || path.ends_with(".wav") || 
                    path.ends_with(".flac") || path.ends_with(".ogg") || path.ends_with(".opus")) {
                    contador++;
                    Cancion can = obtenerInfoCancion(path);
                    if (can.album != "") {
                        bool album_nuevo = true;
                        for (size_t i=0; i < albums.size(); i++) {
                            if (can.album == albums[i].nombre) {
                                albums[i].num_canciones++;
                                album_nuevo = false;
                                break;
                            }
                        }
                        if (album_nuevo) albums.push_back({can.album, 1});
                    }
                }
            }
        }
    } catch (...) { std::cout << "error al abrir el directorio" << std::endl; return 0;}

    if (contador == 0) { // Comprobamos la búsqueda
        std::cout << "No se ha encontrado ninguna cancion en " << color_Carpeta << r << RESET << std::endl;
        return 0;
    } else if (albums.size() == 0) {
        std::cout << "No se ha encontrado ningun album en " << color_Carpeta << r << RESET << std::endl;
        return 0;
    }

    std::cout << std::endl;
    for (size_t i=0; i < albums.size(); i++)
        std::cout << color_Album << albums[i].nombre << RESET << " - " << color_Numeros << albums[i].num_canciones << RESET << " canciones" << std::endl;
    return albums.size();
}

// Muestra por pantalla los controles
void mostrarControles() {
    std::cout << "Comandos (Esta ayuda se muestra con '" << controles_help.controlesToString() << "'):\n| "
        << "Espacio" << (!controles_pause.controles.empty() ? "/" + controles_pause.controlesToString() : "") << ": Play/Pause     | " 
        << controles_salir.controlesToString()      << ": Salir          | "
        << controles_clear.controlesToString()      << ": Limpiar\n| "
        << controles_vol_mas.controlesToString()    << " [%]: Aumentar volumen | "
        << controles_vol_igual.controlesToString()  << " [%]: Igualar volumen | "
        << controles_vol_menos.controlesToString()  << " [%]: Disminuir volumen\n| "
        << controles_atras.controlesToString()      << " [sec]: Echar atras    | "
        << controles_adelante.controlesToString()   << " [sec]: Adelantar     | "
        << controles_reiniciar.controlesToString()  << ": Reiniciar\n| "
        << controles_loop.controlesToString()       << ": Loop                 | "
        << controles_autoplay.controlesToString()   << ": Autoplay            | "
        << controles_shuffle.controlesToString()    << ": Shuffle\n| "
        << controles_anterior.controlesToString()   << " [num]: Anterior       | "
        << controles_siguiente.controlesToString()  << " [num]: Siguiente     | "
        << controles_ir_a.controlesToString()       << ": Ir a cancion\n| "
        << controles_explorador.controlesToString() << ": Explorador           | "
        << controles_buscar.controlesToString()     << ": Buscar              | "
        << controles_mostrar_canciones.controlesToString() << ": Mostrar canciones\n| "
        << controles_velocidad.controlesToString()  << " [num]: Velocidad      |\n| "
        << controles_cargar_carpeta.controlesToString() << ": Cargar carpeta  | "
        << controles_cargar_carpeta.controlesToString() << " -" << flag_cargar_carpeta_reiniciar << ": Reiniciar reproductor | "
        << controles_cargar_carpeta.controlesToString() << " -" << flag_cargar_carpeta_album << ": Cargar solo un album\n\n" << std::endl;
}

// Programa reproductor de música dentro de una carpeta
int main() {
    Player player;
    bool autoplay = true, esperando_carpeta = false, esperando_busqueda = false, esperando_confirmacion_busqueda = false,
         esperando_seleccion = false, cambiar_prompt = false, reiniciar_reproductor = false, esperando_album = false;
    int indiceCancion = 0;
    std::string linea, nuevo_prompt, prompt_actual = "> ", ruta;

    // Leemos el archivo de configuración
    controlesPorDefecto(); // Primero inicializamos los controles con valores por defecto
    if (!cargarOpciones(FICHERO_OPCIONES))
        std::cout << color_Error << "Error" << RESET << ": No se ha podido abrir el fichero de configuraciones \""
                  << color_Error << FICHERO_OPCIONES << RESET << "\"\n"
                  << "Se aplicaran las configuraciones por defecto" << std::endl;

    // Preparamos la thread encargada de leer el teclado
    std::queue<std::string> cola_comandos;
    std::mutex mutex_cola;
    std::thread input_thread(leerComandos, std::ref(cola_comandos), std::ref(mutex_cola), ruta_inicial.empty());
    input_thread.detach();

    // Iniciamos la UI del usuario
    iniciarUI();
    mostrarControles();
    player.asignarVolumen(volumen_inicial);

    // Cargamos la ruta inicial si se configuró
    if (!ruta_inicial.empty()) {
        player.cargarCarpeta(ruta_inicial, true);
        ruta = ruta_inicial;
    }

    while (true) {
        // Comprobar si acabó la canción
        if (player.haTerminado() && autoplay) player.reproducirSiguiente();

        // Procesar comandos pendientes
        { std::lock_guard<std::mutex> lock(mutex_cola);
          while (!cola_comandos.empty()) {
            std::string linea = cola_comandos.front();
            cola_comandos.pop();
            if (linea.empty()) { player.togglePausa(); continue; }

            if (esperando_carpeta) { // Introducir la ruta de una carpeta con 'C'
                Comando carpeta = parsearComando(linea);
                if (controles_salir.controlCorrecto(carpeta.accion)) {
                    std::cout << "\033[2K\r" << "Se ha cancelado cargar carpeta" << std::endl;
                    esperando_carpeta = false;
                    cambiar_prompt = true;
                    nuevo_prompt = "> ";
                } else {
                    if (esperando_album) {
                        if (mostrarAlbumsEnCarpeta(linea) != 0) {
                            std::cout << "Seleccione un album ('" << controles_salir.controlesToString() << "' para cancelar)" << std::endl;
                            esperando_carpeta = false;
                            cambiar_prompt = true;
                            ruta = linea;
                            nuevo_prompt = "Album que cargar: ";
                        }
                    } else {
                        player.cargarCarpeta(linea, reiniciar_reproductor);
                        // Solo volvemos al prompt normal si la carga tuvo éxito
                        if (!player.playerVacioSilencioso()) {
                            esperando_carpeta = false;
                            cambiar_prompt = true;
                            nuevo_prompt = "> ";
                        }
                    }
                }
                continue;
            } else if (esperando_album) { // Introducir el album de una carpeta con "C -a"
                Comando album = parsearComando(linea);
                if (controles_salir.controlCorrecto(album.accion)) {
                    std::cout << "\033[2K\r" << "Se ha cancelado seleccionar album" << std::endl;
                    esperando_album = false;
                    cambiar_prompt = true;
                    nuevo_prompt = "> ";
                } else {
                    player.cargarCarpeta(ruta, reiniciar_reproductor, linea);
                    // Solo volvemos al prompt normal si la carga tuvo éxito
                    if (!player.playerVacioSilencioso()) {
                        esperando_album = false;
                        cambiar_prompt = true;
                        nuevo_prompt = "> ";
                    }
                }
                continue;
            } else if (esperando_busqueda) { // Búsqueda de 'B' - Buscar
                indiceCancion = player.buscarCancion(linea);
                if (indiceCancion != -1) {
                    std::cout << "\033[2K\r" << "Quieres reproducir la cancion "
                     << color_Cancion << player.tituloIndice(indiceCancion) << RESET << "? [y/n]" << std::flush;
                    esperando_confirmacion_busqueda = true;
                }
                esperando_busqueda = false;
                cambiar_prompt = true;
                nuevo_prompt = "> ";
                continue;
            } else if (esperando_confirmacion_busqueda) { // Confirmación de 'B' - Buscar
                Comando confirmacion = parsearComando(linea);
                if (confirmacion.accion == "y" || confirmacion.accion == "yes") {
                    std::cout << std::endl;
                    player.reproducirIndice(indiceCancion);
                    esperando_confirmacion_busqueda = false;
                } else if (confirmacion.accion == "n" || confirmacion.accion == "no") {
                    std::cout << "\033[2K\r" << "No se ha reproducido "
                     << color_Cancion << player.tituloIndice(indiceCancion) << RESET << std::endl;
                    esperando_confirmacion_busqueda = false;
                }
                continue;
            } else if (esperando_seleccion) { // Selección de una opción de 'F' - Explorar
                if (player.seleccionarExplorador(linea)) esperando_seleccion = false;
                continue;
            }

            // Convertimos el string en un comando
            Comando cmd = parsearComando(linea);
            std::string& c = cmd.accion;
            bool tieneValor = cmd.valor != "";
            std::transform(c.begin(), c.end(), c.begin(), ::tolower); // Lo hacemos minúscula

            // Ejecutamos el comando
                 if (controles_pause.controlCorrecto(c)) player.togglePausa(); // Espacio/Enter -> Pausa
            else if (controles_anterior.controlCorrecto(c)) player.reproducirRelativoNegativo(tieneValor ? toInt(cmd.valor) : 1);
            else if (controles_siguiente.controlCorrecto(c)) player.reproducirRelativoPositivo(tieneValor ? toInt(cmd.valor) : 1);
            else if (controles_adelante.controlCorrecto(c)) player.saltar(tieneValor ? toFloat(cmd.valor) : 10.0f);
            else if (controles_atras.controlCorrecto(c)) player.saltar(tieneValor ? -toFloat(cmd.valor) : -10.0f);
            else if (controles_ir_a.controlCorrecto(c)) player.reproducirIndice(tieneValor ? toInt(cmd.valor, -1) - 1 : -1);
            else if (controles_vol_igual.controlCorrecto(c)) player.asignarVolumen(tieneValor ? toFloat(cmd.valor, 50.0f) / 100 : 1.0f);
            else if (controles_vol_mas.controlCorrecto(c)) player.cambiarVolumen(tieneValor ? toFloat(cmd.valor, 10.0f) / 100 : 0.1f);
            else if (controles_vol_menos.controlCorrecto(c)) player.cambiarVolumen(tieneValor ? -toFloat(cmd.valor, 10.0f) / 100 : -0.1f);
            else if (controles_clear.controlCorrecto(c)) reiniciarAreaScroll();
            else if (controles_help.controlCorrecto(c)) mostrarControles();
            else if (controles_mostrar_canciones.controlCorrecto(c)) player.mostrarCanciones();
            else if (controles_shuffle.controlCorrecto(c)) player.toggleShuffle();
            else if (controles_loop.controlCorrecto(c)) player.toggleLoop();
            else if (controles_reiniciar.controlCorrecto(c)) player.reiniciar();
            else if (controles_velocidad.controlCorrecto(c)) player.cambiarPitch(tieneValor ? toFloat(cmd.valor) : 1.0f);
            else if (controles_autoplay.controlCorrecto(c)) {
                if (autoplay) std::cout << "Reproduccion automatica desactivada" << std::endl;
                else          std::cout << "Reproduccion automatica activada" << std::endl;
                autoplay = !autoplay;
            }
            else if (controles_explorador.controlCorrecto(c)) {
                player.iniciarExplorador(tieneValor ? cmd.valor : "");
                esperando_seleccion = true;
            }
            else if (controles_cargar_carpeta.controlCorrecto(c)) {
                reiniciar_reproductor = cmd.tieneFlag('r');
                if (cmd.tieneFlag('a')) esperando_album = true;
                if (!tieneValor || cmd.valor == "") {
                    std::cout << "Escriba el directorio donde estan sus canciones ('" << controles_salir.controlesToString() << "' para cancelar)" << std::endl;
                    cambiar_prompt = true;
                    esperando_carpeta = true;
                    nuevo_prompt = "Directorio donde estan las canciones: ";
                } else {
                    if (esperando_album) {
                        if (mostrarAlbumsEnCarpeta(cmd.valor) != 0) {
                            std::cout << "Seleccione un album ('" << controles_salir.controlesToString() << "' para cancelar)" << std::endl;
                            ruta = cmd.valor;
                            cambiar_prompt = true;
                            nuevo_prompt = "Album que cargar: ";
                        }
                    } else player.cargarCarpeta(cmd.valor, reiniciar_reproductor);
                }
            }
            else if (controles_buscar.controlCorrecto(c)) {
                if (tieneValor) {
                    indiceCancion = player.buscarCancion(cmd.valor);
                    if (indiceCancion != -1) {
                        std::cout << "\033[2K\r" << "Quieres reproducir la cancion "
                         << color_Cancion << player.tituloIndice(indiceCancion) << RESET << "? [y/n]" << std::flush;
                        esperando_confirmacion_busqueda = true;
                    }
                } else {
                    std::cout << "Escriba el titulo de la cancion que quiera buscar" << std::flush;
                    cambiar_prompt = true;
                    nuevo_prompt = "Buscar: ";
                    esperando_busqueda = true;
                }
            }
            else if (controles_salir.controlCorrecto(c)) {
                player.pausar();
                borrarUI();
                reiniciarAreaScroll();
                #ifndef _WIN32
                    restaurarTerminal();
                #endif
                exit(0);
            }
            else if (c == "p") std::cout << "Comando en desarrollo (Por decidir)"      << std::endl;
            else if (c == "t") std::cout << "Comando en desarrollo (Mostrar artistas)" << std::endl;
            else if (c == "g") std::cout << "Comando en desarrollo (Por decidir)"      << std::endl;
            else if (c == "j") std::cout << "Comando en desarrollo (Por decidir)"      << std::endl;
            else if (c == "k") std::cout << "Comando en desarrollo (Por decidir)"      << std::endl;
            else std::cout << "Comando desconocido: " << c << std::endl;
            }
        }

        if (cambiar_prompt) { // Actualizamos el prompt
            std::lock_guard<std::mutex> lock(mutexPrompt);
            prompt_actual = nuevo_prompt;
            cambiar_prompt = false;
        }

        { // Actualizamos la UI
            auto [pos, dur] = player.obtenerPosicionYDuracion();
            std::lock_guard<std::mutex> lock(mutexPrompt);
            actualizarUI(player.obtenerInfoActual(), pos, dur, prompt_actual, input_actual);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Dormimos para no consumir la CPU al 100%
    }
}
