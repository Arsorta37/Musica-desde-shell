#pragma once
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <random>
#include <mutex>
#include <fstream>
#include <climits>

#include "miniaudio.h"
#include "terminal.hpp"
#include "lectorShell.hpp"

const size_t MAX_VENTANA_REP = 200;

struct Cancion {
    std::string ruta;
    std::string titulo;
    std::string album;
    std::string pista;
    int num_pista; // INT_MAX si no tiene tag
};

struct InfoTag {
    std::string titulo;
    std::string album;
    std::string artista;
    std::string interprete;
    std::string pista; // "N/MAX", "N" o ""
    int num_pista = INT_MAX;
};

// Convierte datos UTF-16 (con o sin BOM) a std::string UTF-8
static std::string utf16ToUtf8(const std::vector<char>& datos) {
    std::string out;
    size_t i = 1; // Saltamos el byte de encoding (datos[0])
    bool littleEndian = true;

    // Detectar BOM
    if (i + 1 < datos.size()) {
        uint8_t b1 = static_cast<uint8_t>(datos[i]);
        uint8_t b2 = static_cast<uint8_t>(datos[i + 1]);
        if (b1 == 0xFF && b2 == 0xFE) { littleEndian = true;  i += 2; }
        else if (b1 == 0xFE && b2 == 0xFF) { littleEndian = false; i += 2; }
    }

    while (i + 1 < datos.size()) {
        uint16_t cu;
        if (littleEndian)
            cu = (static_cast<uint8_t>(datos[i + 1]) << 8) | static_cast<uint8_t>(datos[i]);
        else
            cu = (static_cast<uint8_t>(datos[i]) << 8) | static_cast<uint8_t>(datos[i + 1]);
        i += 2;

        if (cu == 0) break; // Terminador nulo de ID3v2

        if (cu < 0x80) {
            out += static_cast<char>(cu);
        } else if (cu < 0x800) {
            out += static_cast<char>(0xC0 | (cu >> 6));
            out += static_cast<char>(0x80 | (cu & 0x3F));
        } else {
            // Par sustituto (caracteres fuera del BMP, ej: emojis)
            if (cu >= 0xD800 && cu <= 0xDBFF && i + 1 < datos.size()) {
                uint16_t low;
                if (littleEndian)
                    low = (static_cast<uint8_t>(datos[i + 1]) << 8) | static_cast<uint8_t>(datos[i]);
                else
                    low = (static_cast<uint8_t>(datos[i]) << 8) | static_cast<uint8_t>(datos[i + 1]);
                i += 2;
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    uint32_t cp = 0x10000 + ((cu - 0xD800) << 10) + (low - 0xDC00);
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                    continue;
                }
            }
            // Plano básico (3 bytes)
            out += static_cast<char>(0xE0 | (cu >> 12));
            out += static_cast<char>(0x80 | ((cu >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cu & 0x3F));
        }
    }
    return out;
}

InfoTag leerID3v2(const std::string& ruta) {
    InfoTag info;
    std::ifstream f(ruta, std::ios::binary);
    if (!f) return info;

    // Cabecera ID3v2: "ID3" + version (2 bytes) + flags (1 byte) + tamaño (4 bytes)
    char header[10];
    f.read(header, 10);
    if (std::string(header, 3) != "ID3") return info; // no tiene ID3v2

    // El tamaño usa 7 bits por byte (syncsafe integer)
    int tagSize = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14)
                | ((header[8] & 0x7F) << 7)  |  (header[9] & 0x7F);
    //int version = header[3]; // 3 = ID3v2.3, 4 = ID3v2.4
    int leido = 0;
    while (leido < tagSize) {
        // Cada frame: ID (4 bytes) + tamaño (4 bytes) + flags (2 bytes) + datos
        char frameHeader[10];
        f.read(frameHeader, 10);
        if (!f || frameHeader[0] == 0) break; // padding

        std::string frameID(frameHeader, 4);
        int frameSize = ((unsigned char)frameHeader[4] << 24) |
                        ((unsigned char)frameHeader[5] << 16) |
                        ((unsigned char)frameHeader[6] << 8)  |
                         (unsigned char)frameHeader[7];
        leido += 10 + frameSize;
        if (frameSize <= 0 || frameSize > 1000000) break;

        std::vector<char> datos(frameSize);
        f.read(datos.data(), frameSize);

        // Los frames de texto empiezan con 1 byte de encoding
        // 0 = Latin-1, 1 = UTF-16, 3 = UTF-8
        if (frameID == "TIT2" || frameID == "TALB" || frameID == "TRCK") {
            std::string valor;
            uint8_t encoding = static_cast<uint8_t>(datos[0]);

            if (encoding == 1) { // UTF-16 con BOM o sin BOM
                valor = utf16ToUtf8(datos);
            } else if (encoding == 3) { // UTF-8 nativo (ID3v2.4+)
                valor = std::string(datos.data() + 1, frameSize - 1);
            } else { // Latin-1 (ISO-8859-1) o UTF-16BE sin BOM (raro)
                valor = std::string(datos.data() + 1, frameSize - 1);
            }

            // Eliminar terminadores nulos residuales
            valor.erase(std::find(valor.begin(), valor.end(), '\0'), valor.end());

            if      (frameID == "TIT2") info.titulo = valor;
            else if (frameID == "TALB") info.album = valor;
            else if (frameID == "TPE1") info.artista = valor; // De momento no las uso
            else if (frameID == "TSOP") info.interprete = valor; // De momento no las uso
            else if (frameID == "TRCK") {
                info.pista = valor;
                std::string n = valor.substr(0, valor.find('/'));
                try { info.num_pista = std::stoi(n); }
                catch (...) { info.num_pista = INT_MAX; }
            }
        }
    }
    return info;
}

Cancion obtenerInfoCancion(const std::string& ruta) {
    Cancion cancion = {ruta, "", "", "", INT_MAX}; // Si no tiene título, será su ruta
    InfoTag tag = leerID3v2(ruta);
    cancion.titulo = tag.titulo;
    cancion.album  = tag.album;
    cancion.pista  = tag.pista;
    cancion.num_pista = tag.num_pista;
    return cancion;
}

struct Explorador {
    std::vector<std::string> subcarpetas;
    std::string carpetaActual;
    bool activo = false;

    void explorar(const std::string& ruta) {
        subcarpetas.clear();
        carpetaActual = ruta;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(ruta))
                if (entry.is_directory())
                    subcarpetas.push_back(entry.path().string());
        } catch (...) {
            std::cout << ROJO << "Error" << RESET << " al leer: " << ROJO << ruta << RESET << std::endl;
            return;
        }
        std::sort(subcarpetas.begin(), subcarpetas.end());
        std::cout << "  0. [Reproducir esta carpeta]" << std::endl;
        std::cout << "  .. [Subir un nivel]" << std::endl;
        for (size_t i = 0; i < subcarpetas.size(); i++) {
            std::string nombre = subcarpetas[i].substr(subcarpetas[i].find_last_of("/\\") + 1);
            std::cout << std::setfill(' ') << std::setw(3) << i+1 << ". " << nombre << std::endl;
        }
        activo = true;
        std::cout << "Selecciona: "; std::cout.flush();
    }

    // Devuelve la carpeta a reproducir, o "" si sigue navegando
    std::string seleccionar(const std::string& input) {
        if (input == "..") {
            std::string padre = std::filesystem::path(carpetaActual).parent_path().string();
            explorar(padre);
            return "";
        }
        int n = -1;
        try { n = std::stoi(input); } catch (...) {}
        if (n == 0) { activo = false; return carpetaActual; }
        if (n >= 1 && n <= (int)subcarpetas.size()) {
            explorar(subcarpetas[n-1]);
            return "";
        }
        std::cout << "Seleccion no valida" << std::endl;
        std::cout << "Selecciona: "; std::cout.flush();
        return "";
    }
};

class Player {
private:
    ma_engine engine;
    ma_sound sonido;
    Explorador exp;
    std::vector<Cancion> canciones;
    std::vector<unsigned> ventanaReproduccion;
    std::string ruta;
    unsigned indiceVentana = 0;
    float volumen = 1.0f;
    bool shuffle = false;
    bool loop = true;
    bool haySonido = false;
    bool pausado = false;

public:
    Player() { inicializarAudio(); }
    ~Player() {
        if (haySonido) ma_sound_uninit(&sonido);
        ma_engine_uninit(&engine);
    }

    void inicializarAudio() {
        if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
            std::cout << ROJO << "Error" << RESET << " inicializando audio\n";
            return;
        }
    }

    // Devuelve true si y solo si el vector <canciones> está vacío
    bool playerVacio() const {
        if (canciones.empty()) std::cout << "No hay ninguna cancion cargada!!" << std::endl;
        return canciones.empty();
    }

    // Función que hace lo mismo que playerVacio() pero sin mensaje
    bool playerVacioSilencioso() const { return canciones.empty(); }

    bool haySonidoActivo() const { return haySonido; }

    // Devuelve true si y solo si hay una canción sonando que ya ha acabado
    bool haTerminado() const {
        if (!haySonido) return false;
        return ma_sound_at_end(&sonido);
    }

    std::pair<float, float> obtenerPosicionYDuracion() {
        if (!haySonido) return {0.0f, 0.0f};
        float pos = 0.0f, dur = 0.0f;
        ma_sound_get_cursor_in_seconds(&sonido, &pos);
        ma_sound_get_length_in_seconds(&sonido, &dur);
        return {pos, dur};
    }

    // Obtener posición actual en segundos
    float obtenerPosicion() {
        float segundos = 0.0f;
        ma_sound_get_cursor_in_seconds(&sonido, &segundos);
        return segundos;
    }

    std::string tituloIndice(const int indice) {
        if (indice < 0 || (unsigned)indice >= canciones.size())
            return (ROJO + (std::string)"Error al leer el titulo" + RESET);
        return CorregirTitulo(canciones[indice].ruta, canciones[indice].titulo);
    }

    void toggleShuffle() {
        if (shuffle) std::cout << "Lista no aleatoria" << std::endl;
            else     std::cout << "Lista aleatorizada" << std::endl;
        shuffle = !shuffle;
    }

    void toggleLoop() {
        loop = !loop;
        if (loop) std::cout << "Loop activado" << std::endl;
            else  std::cout << "Loop desactivado" << std::endl;
    }

    void togglePausa() {
        if (pausado) { reanudar(); } else { pausar(); }
        pausado = !pausado;
    }

    void pausar() {
        if (haySonido) {
            ma_sound_stop(&sonido);
            std::cout << "Cancion pausada" << std::endl;
        }
    }

    void reanudar() {
        if (haySonido) {
            ma_sound_start(&sonido);
            std::cout << "Cancion reanudada" << std::endl;
        }
    }

    void asignarVolumen(float delta) {
        volumen = std::clamp(delta, 0.0f, 1.0f);
        ma_engine_set_volume(&engine, volumen);
        std::cout << "Volumen: " << AMARILLO << (int)(volumen * 100) << "%" << RESET;
        if ((int)(volumen * 100) == 0) std::cout << " - (muteado)";
        else if ((int)(volumen * 100) == 33) std::cout << " - \"For those who come after\"";
        else if ((int)(volumen * 100) == 37) std::cout << " - perfecto";
        else if ((int)(volumen * 100) == 67) std::cout << " >:(";
        else if ((int)(volumen * 100) == 69) std::cout << " - nice";
        else if ((int)(volumen * 100) == 73) std::cout << " - Sheldon Cooper?";
        std::cout << std::endl;
    }

    void cambiarVolumen(float delta) { asignarVolumen(volumen + delta); }

    // Saltar a una posición concreta
    void buscar(float segundos) {
        ma_sound_seek_to_pcm_frame(&sonido, (ma_uint64)(segundos * ma_engine_get_sample_rate(&engine)));
    }

    // Iguala la posición a 0
    void reiniciar() {
        ma_sound_seek_to_pcm_frame(&sonido, (ma_uint64)(0 * ma_engine_get_sample_rate(&engine)));
        std::cout << "Posicion: " << VERDE << "0s" << RESET << std::endl;
    }

    // Adelantar/retroceder delta segundos
    void saltar(float delta) {
        if (!haySonido) return;
        float nuevaPosicion = obtenerPosicion() + delta;
        buscar(std::max(0.0f, nuevaPosicion));
        std::cout << "Posicion: " << VERDE << (int)nuevaPosicion << "s" << RESET << std::endl;
    }

    // Cambia la velocidad (pitch) de la canción
    void cambiarPitch(float pitch) {
        if (!haySonido) return;
        ma_sound_set_pitch(&sonido, pitch);
        std::cout << "Velocidad: " << AMARILLO << "x" << pitch << RESET << std::endl;
    }

    // Reproduce la canción en la posición <indice> en el vector <canciones>
    void reproducirIndice(const int indice, const bool mostrar=true) {
        if (playerVacio()) return;
        if (indice < 0 || (unsigned)indice >= canciones.size()) return;
        ventanaReproduccion.push_back(indice);
        indiceVentana = ventanaReproduccion.size() - 1;
        reproducirActual(mostrar);
    }

    // Busca una canción por título y devuelve su índice en el vector <canciones>
    int buscarCancion(const std::string& query) const {
        if (playerVacio()) return -1;
        std::string queryLower = query;
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

        for (size_t i = 0; i < canciones.size(); i++) {
            std::string titulo = CorregirTitulo(canciones[i].ruta, canciones[i].titulo);
            std::string tituloLower = titulo;
            std::transform(tituloLower.begin(), tituloLower.end(), tituloLower.begin(), ::tolower);

            if (tituloLower.find(queryLower) != std::string::npos) return i;
        }
        std::cout << "No se encontro ninguna cancion con \"" << ROJO << query << RESET << "\"" << std::endl;
        return -1;
    }

    // Si no tiene título se usa su ruta sin las carpetas padre
    std::string CorregirTitulo(const std::string& ruta, const std::string& titulo) const {
        if (!titulo.empty()) return titulo;
        std::string resultado;
        size_t barra1 = ruta.find_last_of('/');
        size_t barra2 = ruta.find_last_of('\\');
        size_t barra = std::string::npos;
        if (barra1 != std::string::npos) barra = barra1;
        if (barra2 != std::string::npos && (barra == std::string::npos || barra2 > barra)) barra = barra2;
        if (barra != std::string::npos) resultado = ruta.substr(barra + 1);
        return resultado;
    }

    // Devuelve un string con el album, la pista, el título y la duración(si se quiere) de la canción que está sonando actualmente
    std::string obtenerInfoActual(const bool duracion=false) const {
        if (canciones.empty() || ventanaReproduccion.empty()) return "Sin cancion";
        const Cancion& cancion = canciones[ventanaReproduccion[indiceVentana]];
        std::string info = "";

        if (!cancion.album.empty() || !cancion.pista.empty()) // Solo escribimos el album y pista si existen
            info += (cancion.album.empty() ? "?" : cancion.album) + " [" + (cancion.pista.empty() ? "?" : cancion.pista) + "] - ";
        info += CorregirTitulo(cancion.ruta, cancion.titulo);

        if (duracion) {
            float duracion = 0.0f; // Calculamos los minutos y segundos que va a durar
            ma_sound_get_length_in_seconds(&sonido, &duracion);
            info += " (" + (std::string)AZUL + secToString(duracion) + RESET + ")";
        }
        return info;
    }

    // Muestra el album, la pista, el título y la duración de la canción que está sonando actualmente
    void mostrarInfoCancionActual() const {
        std::cout << VERDE << NEGRITA << "Sonando: " << RESET << obtenerInfoActual(true) << std::endl;
    }

    // Muestra todas las canciones del vector <canciones>
    void mostrarCanciones() const {
        if (playerVacio()) return;
        int filas = obtenerFilasTerminal();
        int paginaTam = filas - 6; // líneas disponibles menos UI y margen
        unsigned total = canciones.size();
        unsigned pagina = 0;
        unsigned totalPaginas = (total + paginaTam - 1) / paginaTam;

        // Buscar la página donde está la canción actual
        unsigned actual = ventanaReproduccion[indiceVentana];
        pagina = actual / paginaTam;

        auto mostrarPagina = [&]() {
            std::cout << "\033[2J\033[1;1H"; // limpiar área
            std::cout << "Canciones cargadas (pag " << pagina+1 << "/" << totalPaginas
                    << ", usa a/d para navegar, cualquier otra tecla para salir):" << std::endl;
            unsigned inicio = pagina * paginaTam;
            unsigned fin = std::min(inicio + paginaTam, total);
            for (unsigned i = inicio; i < fin; i++) {
                const Cancion& c = canciones[i];
                std::string marca = (i == ventanaReproduccion[indiceVentana]) ? "\033[32m>>\033[0m" : "  ";
                std::cout << marca << std::setfill(' ') << std::setw(3) << i+1 << ". " << CorregirTitulo(c.ruta, c.titulo);
                if (!c.album.empty() || !c.pista.empty()) // Solo escribimos el album y pista si existen
                    std::cout << " - " << CYAN << (c.album.empty() ? "?" : c.album) << RESET << " [" << (c.pista.empty() ? "?" : c.pista) << "]";
                std::cout << std::endl;
            }
        };

        mostrarPagina();
        inputBloqueado = true; // bloquear hilo de input

        // Leer navegación directamente (ya estamos en modo raw)
        char ch;
        while (true) {
            #ifdef _WIN32
                ch = _getch();
            #else
                read(STDIN_FILENO, &ch, 1);
            #endif
            if (ch == 'd') { if (pagina < totalPaginas-1) {pagina++; mostrarPagina();} }
            else if (ch == 'a') { if (pagina > 0) {pagina--; mostrarPagina();} }
            else break; // cualquier otra tecla sale
        }
        std::cout << "\033[2J\033[1;1H" << std::flush; // limpiar al salir
        inputBloqueado = false; // desbloquear hilo de input
    }

    // Se empieza a reproducir la canción a la que apunta el índice y muestra su información si no está puesto a false
    void reproducirActual(const bool mostrar=true) {
        if (playerVacio()) return;
        std::string r = canciones[ventanaReproduccion[indiceVentana]].ruta;
        if (haySonido) ma_sound_uninit(&sonido);
        if (ma_sound_init_from_file(&engine, r.c_str(), 0, NULL, NULL, &sonido) != MA_SUCCESS) {
            if (mostrar) std::cout << ROJO << "Error" << RESET << " cargando el sonido \"" << ROJO << r << RESET << "\"" << std::endl;
            return;
        }
        ma_sound_start(&sonido);
        haySonido = true;
        if (mostrar) mostrarInfoCancionActual();
    }

    // Si <reiniciar> es verdadero, vacía el reproductor. De cualquier manera, se cargan las canciones de la carpeta seleccionada
    void cargarCarpeta(const std::string& r, const bool reiniciar=false, const std::string album="") {
        // Vaciamos todos los vectores
        if (reiniciar) {
            std::cout << "Iniciando reproductor..." << std::endl;
            ruta = r;
            canciones.clear();
            ventanaReproduccion.clear();
            indiceVentana = 0;
        }

        // Buscamos los archivos
        Cancion cancionActual;
        std::vector<Cancion> cancionesNuevas;
        cancionesNuevas.clear();
        std::cout << "\033[2K\r" << "Cargando canciones desde " << MAGENTA << r << RESET << "... ";
        try {
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(r)) {
                if (entry.is_regular_file()) {
                    std::string path = entry.path().string();
                    if (path.ends_with(".mp3") || path.ends_with(".flac") || 
                        path.ends_with(".wav") || path.ends_with(".opus") || path.ends_with(".ogg")) {
                        cancionActual = obtenerInfoCancion(path);
                        if (album == "" || cancionActual.album == album)
                            cancionesNuevas.push_back(cancionActual);
                    }
                }
            }
        } catch (...) { std::cout << "error al abrir el directorio" << std::endl; return;}

        if (cancionesNuevas.size() == 0) { // Comprobamos la búsqueda
            std::cout << "No se ha encontrado ninguna cancion en " << MAGENTA << r << RESET;
            if (album == "") std::cout << " del album " << CYAN << album << RESET;
            std::cout << std::endl;
            return;
        }

        std::cout << AMARILLO << cancionesNuevas.size() << RESET << " canciones cargadas correctamente (usa 'M' para verlas)" << std::endl;

        // Ordenamos la lista
        std::sort(cancionesNuevas.begin(), cancionesNuevas.end(), [](const Cancion& a, const Cancion& b) {
            if (a.album != b.album) { // ordena primero por album
                if (a.album == "") return false;
                if (b.album == "") return true;
                return  a.album < b.album;
            } else {
                if (a.num_pista != b.num_pista) // ordena segundo por pista
                    return a.num_pista < b.num_pista;
                return  a.ruta < b.ruta; // por último, orden alfabético
            }
        });

        canciones.insert(canciones.end(), cancionesNuevas.begin(), cancionesNuevas.end());

        if (reiniciar) {
            if (shuffle) { // Inicializamos la reproducción
                static std::mt19937 g(std::random_device{}());
                std::uniform_int_distribution<> dist(0, canciones.size() - 1);
                ventanaReproduccion.push_back(dist(g));
            } else {
                ventanaReproduccion.push_back(0);
            }

            reproducirActual();
        }
    }

    // Función auxiliar para reproducirSiguiente() y para obtenerIndiceRelativo()
    unsigned obtenerSiguienteIndice() const {
        if (shuffle) {
            unsigned nuevoIndice;
            do {
                static std::mt19937 g(std::random_device{}());
                std::uniform_int_distribution<> dist(0, canciones.size() - 1);
                nuevoIndice = dist(g);
            } while (nuevoIndice == ventanaReproduccion[indiceVentana] && canciones.size() > 1);
            return nuevoIndice;
        } else {
            if (loop || ventanaReproduccion[indiceVentana] != canciones.size() -1) {
                return (ventanaReproduccion[indiceVentana] + 1) % canciones.size();
            } else {
                return ventanaReproduccion[indiceVentana];
            }
        }
    }

    void reproducirRelativoPositivo(const int indice=1, const bool mostrar=true) {
        if (playerVacio()) return;
        if (indice == 0) return;
        if (indice < 0) {
            reproducirRelativoNegativo(-indice);
            return;
        }

        unsigned auxIndice = (unsigned)indice;
        // Comprovamos que indiceVentana está al final de la ventana
        if (indiceVentana < ventanaReproduccion.size() -1) {
            // Sino, calculamos lo que cuesta moverlo al final
            if (ventanaReproduccion.size() -1 -indiceVentana >= (unsigned)indice) {
                // Si cuesta más que <indice> entero, solo movemos indiceVentana
                indiceVentana += indice;
                reproducirActual(mostrar);
                return;
            } else {
                // Sino, recalculamos el indice y movemos indiceVentana
                auxIndice -= (ventanaReproduccion.size() -1 -indiceVentana);
                indiceVentana = ventanaReproduccion.size() -1;
            }
        }

        if (shuffle) { // Como es aleatorio, de igual cuanto se avance
            reproducirSiguiente(mostrar);
            return;
        }

        unsigned indiceReproducir = 0;
        if (loop) {
            indiceReproducir = ventanaReproduccion[indiceVentana] + auxIndice % canciones.size();
        } else { // !loop
            if (ventanaReproduccion[indiceVentana] + auxIndice >= canciones.size()) {
                if (mostrar) std::cout << "No hay mas canciones en la lista" << std::endl;
                indiceReproducir = canciones.size() - 1;
            } else {
                indiceReproducir = ventanaReproduccion[indiceVentana] + auxIndice;
            }
        }

        if (indiceReproducir != ventanaReproduccion[indiceVentana])
            reproducirIndice(indiceReproducir);
        else reproducirActual(mostrar);
    }

    void reproducirRelativoNegativo(const int indice=1, const bool mostrar=true) {
        if (playerVacio()) return;
        if (indice == 0) return;
        if (indice < 0) {
            reproducirRelativoPositivo(-indice);
            return;
        }

        unsigned indiceVentanaInicial = (unsigned)indiceVentana;
        if ((int)indiceVentana - indice < 0) {
            if (mostrar) std::cout << "No hay canciones anteriores" << std::endl;
            indiceVentana = 0;
        } else indiceVentana -= indice;
        if (indiceVentana != indiceVentanaInicial) reproducirActual(mostrar);
    }

    void reproducirSiguiente(const bool mostrar=true) {
        if (playerVacio()) return;
        if (indiceVentana == ventanaReproduccion.size() - 1) {
            // Conservamos el tamaño máximo de la ventana
            while (ventanaReproduccion.size() > MAX_VENTANA_REP) {
                ventanaReproduccion.erase(ventanaReproduccion.begin());
                indiceVentana--;
            }
            // Añadimos la siguiente canción
            unsigned siguiente = obtenerSiguienteIndice();
            if (!loop && !shuffle && siguiente == ventanaReproduccion.back()) {
                if (mostrar) std::cout << "No hay mas canciones en la lista" << std::endl;
                return;
            } else ventanaReproduccion.push_back(siguiente);
        }
        indiceVentana++;
        reproducirActual(mostrar);
    }

    // Actualmente no se usa porque ya está reproducirRelativoNegativo()
    void reproducirAnterior(const bool mostrar=true) {
        if (playerVacio()) return;
        if (indiceVentana <= 0) {
            indiceVentana = 0;
            if (mostrar) std::cout << "No hay canciones anteriores" << std::endl;
        } else indiceVentana--;
        reproducirActual(mostrar);
    }

    void iniciarExplorador(const std::string& r = "") {
        exp.explorar(r.empty() ? ruta : r);
    }

    // Devuelve true si ha cargado una carpeta
    bool seleccionarExplorador(const std::string& input) {
        std::string resultado = exp.seleccionar(input);
        if (!resultado.empty()) { cargarCarpeta(resultado); return true; }
        return false;
    }

    bool exploradorActivo() { return exp.activo; }
};
