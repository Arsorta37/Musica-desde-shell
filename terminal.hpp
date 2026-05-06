#include <iostream>
#include <string>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif

// Colores de texto
#define RESET "\033[0m"
#define ROJO "\033[31m"
#define VERDE "\033[32m"
#define AMARILLO "\033[33m"
#define AZUL "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define NEGRITA "\033[1m"
#define FONDO_AZUL "\033[44m"

// Función auxiliar para mostrarCanciones(), iniciarUI() y actualizarUI()
#ifdef _WIN32
    #include <windows.h>
    int obtenerFilasTerminal() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    #include <sys/ioctl.h>
    int obtenerFilasTerminal() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return w.ws_row;
    }
#endif

std::string secToString(const float sec) {
    int m = (int)sec / 60, sg = (int)sec % 60;
    return std::to_string(m) + ":" + (sg < 10 ? "0" : "") + std::to_string(sg);
};

// Reserva las 3 líneas de la UI
void iniciarUI() {
    #ifndef _WIN32
        activarModoRaw();
        atexit(restaurarTerminal);
    #endif

    std::cout << "\033[2J\033[1;1H" << std::flush; // borra pantalla y va a la línea 1

    int filas = obtenerFilasTerminal();
    std::cout << "\033[1;" << filas-3 << "r" // área de scroll: líneas 1 a filas-3
              << "\033[1;1H" << std::flush; // cursor a la línea 1 (dentro del área)
}

void actualizarUI(const std::string& infoCancion, float pos, float dur,
                  const std::string& prompt, const std::string& inputActual, int ancho = 50) {
    float pct = dur > 0 ? pos / dur : 0;
    int relleno = (int)(pct * ancho);

    std::string barra = VERDE + secToString(pos) + RESET + " [" + VERDE;
    for (int i = 0; i < ancho; i++)
        barra += (i < relleno ? '=' : (i == relleno ? '>' : ' '));
    barra = barra + RESET + "] " + AZUL + secToString(dur) + RESET;

    int filas = obtenerFilasTerminal();
    std::cout
        << "\033[s" // guarda posición
        << "\033[" << filas-2 << ";1H"
        << "\033[2K" << infoCancion << std::endl
        << "\033[2K" << barra << std::endl
        << "\033[2K" << prompt << inputActual
        << "\033[u" // restaura posición (dentro del área de scroll)
        << std::flush;
}
