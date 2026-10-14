/* ================================================================
 * cifrar.cpp - Archivo de hasta 350 palabras -> DES-ECB sin cabecera.
 * Uso: ./cifrar <entrada.txt> <salida.bin> <llave> [--legacy]
 * --legacy interpreta la llave como el entero little-endian anterior.
 * CC3069 - Proyecto #2, UVG.
 * ================================================================ */
#include <cctype>
#include <filesystem>
#include "app_utils.h"

/* Valida argumentos y limite de palabras, agrega espacios hasta un
 * multiplo de 8 y comprueba la escritura, incluido el cierre del archivo.
 * Salida: 0 si se genero el cifrado, 1 si alguna operacion fallo. */
int main(int argc, char **argv) {
    try {
        if (argc < 4 || argc > 5 || (argc == 5 && std::string(argv[4]) != "--legacy")) {
            throw std::runtime_error(std::string("Uso: ") + argv[0] +
                " <entrada.txt> <salida.bin> <llave> [--legacy]");
        }
        const bool legacy = argc == 5;
        const KeyType entrada_llave = leer_numero(argv[3], "llave", 0,
            legacy ? std::numeric_limits<KeyType>::max() : ESPACIO_DES - 1);
        const KeyType llave = legacy ? llave_legacy_a_efectiva(entrada_llave) : entrada_llave;
        std::error_code error_ruta;
        if (std::filesystem::equivalent(argv[1], argv[2], error_ruta)) {
            throw std::runtime_error("entrada y salida deben ser archivos diferentes");
        }
        auto texto = leer_archivo(argv[1], false);
        std::size_t palabras = 0;
        bool en_palabra = false;
        for (unsigned char c : texto) {
            if (std::isspace(c)) en_palabra = false;
            else if (!en_palabra) {
                en_palabra = true;
                ++palabras;
            }
        }
        if (palabras > 350) throw std::runtime_error("el texto excede el maximo de 350 palabras");

        const auto len_original = texto.size();
        texto.resize(texto.size() + (8 - texto.size() % 8) % 8, ' ');
        encrypt(llave, texto.data(), texto.size());
        std::ofstream salida(argv[2], std::ios::binary);
        if (!salida) throw std::runtime_error(std::string("no se pudo crear '") + argv[2] + "'");
        salida.write(texto.data(), static_cast<std::streamsize>(texto.size()));
        salida.close();
        if (!salida) throw std::runtime_error(std::string("fallo la escritura de '") + argv[2] + "'");

        std::cout << "Archivo cifrado generado: " << argv[2] << '\n'
                  << "Longitud original: " << len_original << " bytes; cifrada: "
                  << texto.size() << " bytes; palabras: " << palabras << '\n';
        if (legacy) std::cout << "Llave legacy     : " << entrada_llave << '\n';
        std::cout << "Llave efectiva   : " << llave << '\n'
                  << "Llave DES (hex)  : " << llave_hex(llave) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
