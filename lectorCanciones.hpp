#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

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