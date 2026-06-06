#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

struct Cancion {
    std::string ruta;
    std::string titulo;
    std::string album;
    std::string artista;
    std::string interprete;
    std::string pista; // "N/MAX", "N" o ""
    int num_pista = INT_MAX; // INT_MAX si no tiene tag
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

// Extrae y decodifica el valor de texto de un frame (datos crudos del frame, sin cabecera)
// Los frames de texto empiezan con 1 byte de encoding: 0 = Latin-1, 1 = UTF-16, 3 = UTF-8
std::string extraerValorFrame(const std::vector<char>& datos, int frameSize) {
    if (frameSize <= 0 || datos.empty()) return "";
    uint8_t encoding = static_cast<uint8_t>(datos[0]);
    std::string valor;
    if (encoding == 1) valor = utf16ToUtf8(datos);  // UTF-16 con BOM o sin BOM
    else               valor = std::string(datos.data() + 1, frameSize - 1); // Latin-1 o UTF-8 nativo (ID3v2.4+)
    // Eliminar terminadores nulos residuales
    valor.erase(std::find(valor.begin(), valor.end(), '\0'), valor.end());
    return valor;
}

// Asigna el valor de un frame a su campo correspondiente en Cancion.
// Acepta tanto los IDs de v2.2 (3 chars: TT2, TAL, TRK) como los de v2.3/v2.4 (4 chars: TIT2, TALB, TRCK)
void asignarFrame(Cancion& info, const std::string& frameID, const std::string& valor) {
    if      (frameID == "TIT2" || frameID == "TT2") info.titulo = valor;
    else if (frameID == "TALB" || frameID == "TAL") info.album = valor;
    else if (frameID == "TPE1" || frameID == "TP1") info.artista = valor;
    else if (frameID == "TSOP" || frameID == "TSP") info.interprete = valor;
    else if (frameID == "TRCK" || frameID == "TRK") {
        info.pista = valor;
        std::string n = valor.substr(0, valor.find('/'));
        try { info.num_pista = std::stoi(n); } catch (...) { info.num_pista = INT_MAX; }
    }
}

// Lee frames de ID3v2.2: cabecera de 6 bytes (3 ID + 3 tamaño, sin flags)
void leerFramesV2(std::ifstream& f, Cancion& info, int tagSize) {
    int leido = 0;
    while (leido < tagSize) {
        // Cada frame v2.2: ID (3 bytes) + tamaño (3 bytes) + datos
        char fh[6];
        f.read(fh, 6);
        if (!f || fh[0] == 0) break; // padding o fin de tag
        std::string frameID(fh, 3);
        int frameSize = ((unsigned char)fh[3] << 16) | ((unsigned char)fh[4] << 8)
                      |  (unsigned char)fh[5];
        leido += 6 + frameSize;
        if (frameSize <= 0 || frameSize > 1000000) break;

        std::vector<char> datos(frameSize);
        f.read(datos.data(), frameSize);
        asignarFrame(info, frameID, extraerValorFrame(datos, frameSize));
    }
}

// Lee frames de ID3v2.3 y v2.4: cabecera de 10 bytes (4 ID + 4 tamaño + 2 flags)
void leerFramesV3(std::ifstream& f, Cancion& info, int tagSize) {
    int leido = 0;
    while (leido < tagSize) {
        // Cada frame v2.3/v2.4: ID (4 bytes) + tamaño (4 bytes) + flags (2 bytes) + datos
        char fh[10];
        f.read(fh, 10);
        if (!f || fh[0] == 0) break; // padding o fin de tag
        std::string frameID(fh, 4);
        int frameSize = ((unsigned char)fh[4] << 24) | ((unsigned char)fh[5] << 16)
                      | ((unsigned char)fh[6] << 8)  |  (unsigned char)fh[7];
        leido += 10 + frameSize;
        if (frameSize <= 0 || frameSize > 1000000) break;

        std::vector<char> datos(frameSize);
        f.read(datos.data(), frameSize);
        asignarFrame(info, frameID, extraerValorFrame(datos, frameSize));
    }
}

// Lee toda la información de la cabecera y la asigna a las tags de la canción
Cancion obtenerInfoCancion(const std::string& ruta) {
    Cancion info = {ruta, "", "", "", "", "", INT_MAX};
    std::ifstream f(ruta, std::ios::binary);
    if (!f) return info;

    // Cabecera ID3v2: "ID3" + versión (2 bytes) + flags (1 byte) + tamaño (4 bytes)
    char header[10];
    f.read(header, 10);
    if (std::string(header, 3) != "ID3") return info; // no tiene ID3v2

    int version = header[3]; // 2 = ID3v2.2, 3 = ID3v2.3, 4 = ID3v2.4
    // El tamaño usa 7 bits por byte (syncsafe integer) para evitar falsos sync MPEG
    int tagSize = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14)
                | ((header[8] & 0x7F) << 7)  |  (header[9] & 0x7F);

    if (version == 2) leerFramesV2(f, info, tagSize);
    else if (version == 3 || version == 4) leerFramesV3(f, info, tagSize);

    return info;
}
