#pragma once
#include <iostream>
#include <string>
#include <sstream>

#include "lectorOpciones.hpp"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif

// Función auxiliar para mostrarCanciones(), iniciarUI() y actualizarUI()
#ifdef _WIN32
    int obtenerFilasTerminal() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    int obtenerColumnasTerminal() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
#else
    int obtenerFilasTerminal() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return w.ws_row;
    }
    int obtenerColumnasTerminal() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return w.ws_col;
    }
#endif

// Carácter usado para la línea separadora de la UI
const char SEPARADOR_UI = '-';

std::string secToString(const float sec) {
    int min = (int)sec / 60, sg = (int)sec % 60;
    return std::to_string(min) + ":" + (sg < 10 ? "0" : "") + std::to_string(sg);
}

// ───────────────────────────────────────────────────────────────────────────────────────────────────
// SafeStreamBuf: intercepta std::cout para que nunca escriba en las 3 filas de la UI.
// Acumula el texto hasta recibir '\n', entonces lo vuelca dentro área de scroll y restaura el cursor.
// ───────────────────────────────────────────────────────────────────────────────────────────────────
class SafeStreamBuf : public std::streambuf {
public:
    explicit SafeStreamBuf(std::streambuf* original) : original_(original), filaActual_(1) {}

    // Llamado cuando se hace flush explícito (std::flush, std::endl, etc.)
    int sync() override {
        volcarLineaParcial(linea_); // muestra sin avanzar fila (la línea puede continuar)
        original_->pubsync();
        return 0;
    }

    // La siguiente línea empezará desde arriba
    void resetFila() { filaActual_ = 1; }

protected:
    // Llamado carácter a carácter
    int overflow(int c) override {
        if (c == EOF) return EOF;

        char ch = static_cast<char>(c);
        if (ch == '\n') {
            volcarLinea(linea_);
            linea_.clear();
        } else {
            linea_ += ch;
        }
        return c;
    }

    // Llamado para bloques de texto (más eficiente que overflow carácter a carácter)
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) {
            if (overflow(static_cast<unsigned char>(s[i])) == EOF)
                return i;
        }
        return n;
    }

private:
    std::streambuf* original_;
    std::string linea_;
    int filaActual_; // fila donde escribirá la próxima línea (dentro del área de scroll)

    // Escribe texto en la fila actual sin avanzar filaActual_.
    // Usado por sync() para mostrar líneas parciales (prompts, confirmaciones).
    void volcarLineaParcial(const std::string& texto) {
        if (texto.empty()) return;
        int filas = obtenerFilasTerminal();
        int ultima_scroll = filas - 4;
        int fila = std::min(filaActual_, ultima_scroll); // no salirse del área
        std::string seq;
        seq += "\033[s";
        seq += "\033[" + std::to_string(fila) + ";1H";
        seq += "\033[2K";
        seq += texto;
        seq += "\033[u";
        original_->sputn(seq.data(), static_cast<std::streamsize>(seq.size()));
        original_->pubsync();
    }

    // Escribe una línea completa (terminada en \n) en la zona de scroll.
    // Mientras el área no esté llena, escribe de arriba a abajo (filaActual_).
    // Cuando se llena, empuja con "\033[S" para hacer scroll natural.
    void volcarLinea(const std::string& texto) {
        if (texto.empty()) return;

        int filas = obtenerFilasTerminal();
        int ultima_scroll = filas - 4; // última fila del área de scroll

        std::string seq;
        seq += "\033[s"; // guarda cursor (el del prompt)

        if (filaActual_ <= ultima_scroll) {
            // Área aún no llena: escribir en la fila actual y bajar
            seq += "\033[" + std::to_string(filaActual_) + ";1H";
            seq += "\033[2K";
            seq += texto;
            filaActual_++;
        } else {
            // Área llena: scroll natural — ir a la última fila y empujar hacia arriba
            seq += "\033[" + std::to_string(ultima_scroll) + ";1H";
            seq += "\033[S"; // scroll up dentro del área de scroll
            seq += "\033[2K";
            seq += texto;
        }

        seq += "\033[u"; // restaura cursor
        original_->sputn(seq.data(), static_cast<std::streamsize>(seq.size()));
        original_->pubsync();
    }
};

// Buffer global instalado en iniciarUI()
inline SafeStreamBuf* g_safeBuf = nullptr;
inline std::streambuf* g_originalBuf = nullptr;

// ─────────────────────────────────────────────────────────────────────────────

// Variables globales para comparar la UI (evitar redibujar lo que no cambió)
inline std::string linea_separador = "";
inline std::string linea_cancion = "";
inline std::string linea_tiempo = "";
inline std::string linea_input = "";

// Limpia la zona de scroll y reestablece el área de scroll + UI completa.
// Usar en lugar de "\033[2J\033[1;1H" en cualquier parte del programa.
void reiniciarAreaScroll() {
    int filas = obtenerFilasTerminal();
    int cols  = obtenerColumnasTerminal();
    std::string separador(cols/separador_UI.size(), SEPARADOR_UI);
    linea_separador = separador; // sincronizamos el estado
    linea_cancion = linea_tiempo = linea_input = ""; // forzar redibujado de la UI

    std::string seq;
    // Borramos toda la área de scroll
    for (int f = 1; f <= filas - 4; ++f) {
        seq += "\033[" + std::to_string(f) + ";1H";
        seq += "\033[2K";
    }

    seq += "\033[" + std::to_string(filas-3) + ";1H" + separador; // dibuja el separador
    seq += "\033[1;1H"; // cursor a la línea 1
    
    g_originalBuf->sputn(seq.data(), static_cast<std::streamsize>(seq.size()));
    g_originalBuf->pubsync();
    if (g_safeBuf) g_safeBuf->resetFila(); // la próxima línea empieza desde arriba
}

// Reserva las 3 líneas de la UI e instala el SafeStreamBuf
void iniciarUI() {
    #ifndef _WIN32
        activarModoRaw();
        atexit(restaurarTerminal);
    #endif

    // Redirigir std::cout al buffer seguro
    g_originalBuf = std::cout.rdbuf();
    g_safeBuf = new SafeStreamBuf(g_originalBuf);
    std::cout.rdbuf(g_safeBuf);

    // Escribimos directamente al buffer original para la inicialización,
    // ya que la UI todavía no existe y no hay nada que proteger.
    int filas = obtenerFilasTerminal();
    int cols  = obtenerColumnasTerminal();
    std::string separador_actual = ""; // Inicializamos el separador de la UI
    for (int i=0; i < cols/(int)separador_UI.size(); i++)
        separador_actual += separador_UI;

    std::string init;
    init += "\033[2J\033[1;1H";                        // borra pantalla, cursor a (1,1)
    init += "\033[1;" + std::to_string(filas-4) + "r"; // área de scroll: filas 1..filas-4
    init += "\033[" + std::to_string(filas-3) + ";1H"; // va a la fila del separador
    init += separador_actual;                          // dibuja el separador
    init += "\033[1;1H";                               // cursor a la línea 1
    g_originalBuf->sputn(init.data(), static_cast<std::streamsize>(init.size()));
    g_originalBuf->pubsync();
}

void actualizarUI(const std::string& infoCancion, float pos, float dur,
                  const std::string& prompt, const std::string& inputActual, int ancho = 50) {
    float pct = dur > 0 ? pos / dur : 0;
    int relleno = (int)(pct * ancho);
    int filas = obtenerFilasTerminal();
    int cols  = obtenerColumnasTerminal();

    std::string barra = color_Tiempo_actual + secToString(pos) + RESET + " [" + color_Barra_progreso;
    for (int i = 0; i < ancho; i++)
        barra += (i < relleno ? '=' : (i == relleno ? '>' : ' '));
    barra = barra + RESET + "] " + color_Duracion + secToString(dur) + RESET;

    // Si ninguna línea cambió, no hay nada que hacer
    std::string nueva_input = prompt + inputActual;
    std::string separador_actual = ""; // Inicializamos el separador de la UI
    for (int i=0; i < cols/(int)separador_UI.size(); i++)
        separador_actual += separador_UI;

    if (infoCancion == linea_cancion && barra == linea_tiempo && nueva_input == linea_input
        && separador_actual == linea_separador)
        return;

    // Construimos toda la secuencia y la mandamos directamente al buffer original para que SafeStreamBuf no la intercepte.
    std::string seq;
    seq += "\033[s";  // guarda posición del cursor del prompt

    if (separador_actual != linea_separador) {
        linea_separador = separador_actual;
        seq += "\033[" + std::to_string(filas-3) + ";1H";
        seq += "\033[2K" + separador_actual;
    }
    seq += "\033[" + std::to_string(filas-2) + ";1H";

    if (infoCancion != linea_cancion) {
        linea_cancion = infoCancion;
        seq += "\033[2K" + infoCancion;
    }
    seq += "\033[" + std::to_string(filas-1) + ";1H";

    if (barra != linea_tiempo) {
        linea_tiempo = barra;
        seq += "\033[2K" + barra;
    }
    seq += "\033[" + std::to_string(filas) + ";1H";

    if (nueva_input != linea_input) {
        linea_input = nueva_input;
        seq += "\033[2K" + prompt + inputActual;
    }

    seq += "\033[u";  // restaura la posición del cursor

    g_originalBuf->sputn(seq.data(), static_cast<std::streamsize>(seq.size()));
    g_originalBuf->pubsync();
}

void borrarUI() {
    int filas = obtenerFilasTerminal();
    std::string seq;
    for (int f = filas - 3; f <= filas; ++f) {
        seq += "\033[" + std::to_string(f) + ";1H";
        seq += "\033[2K";
    }
    seq += "\033[" + std::to_string(filas - 3) + ";1H";
    g_originalBuf->sputn(seq.data(), static_cast<std::streamsize>(seq.size()));
    g_originalBuf->pubsync();

    // Sincronizamos el estado para forzar redibujado cuando vuelva la UI
    linea_separador = linea_cancion = linea_tiempo = linea_input = "";
}
