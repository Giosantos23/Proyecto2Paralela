/* ================================================================
 * cifrar.cpp
 * ----------------------------------------------------------------
 * Utilitario para generar textos cifrados de prueba. Lee un archivo
 * de texto plano (.txt), lo rellena a un multiplo de 8 bytes, lo
 * cifra con DES usando una llave privada dada y escribe el resultado
 * binario (.bin) que consumen bruteforce_seq / bruteforce_mpi.
 *
 * Uso:
 *   ./cifrar <entrada.txt> <salida.bin> <llave>
 *
 *   entrada.txt : texto plano a cifrar (max 350 palabras por enunciado)
 *   salida.bin  : archivo cifrado de salida
 *   llave       : valor numerico de la llave privada (KeyType)
 *
 * Proyecto #2 - Computacion Paralela y Distribuida (CC3069) - UVG
 * ================================================================ */
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "des_utils.h"

/* ----------------------------------------------------------------
 * Funcion: main
 * Proposito: Valida argumentos, carga el texto plano, aplica relleno
 *            con espacios hasta un multiplo de 8, cifra con la llave
 *            indicada y guarda el binario resultante.
 * ---------------------------------------------------------------- */
int main(int argc, char **argv) {
    /* --- Programacion defensiva --- */
    if (argc < 4) {
        std::cerr << "Uso: " << argv[0]
                  << " <entrada.txt> <salida.bin> <llave>\n";
        return 1;
    }
    std::string ruta_in = argv[1];
    std::string ruta_out = argv[2];
    KeyType llave = (KeyType)std::stoull(argv[3]);

    /* --- Lectura del texto plano --- */
    std::ifstream entrada(ruta_in, std::ios::binary | std::ios::ate);
    if (!entrada.is_open()) {
        std::cerr << "Error: no se pudo abrir '" << ruta_in << "'\n";
        return 1;
    }
    std::streamsize tam = entrada.tellg();
    entrada.seekg(0, std::ios::beg);
    if (tam <= 0) {
        std::cerr << "Error: archivo de entrada vacio\n";
        return 1;
    }

    /* Relleno (padding) con espacios hasta multiplo de 8, requisito
     * del cifrado por bloques DES-ECB. */
    int len = (int)tam;
    int padding = (len % 8 == 0) ? 0 : (8 - len % 8);
    std::vector<char> texto(len + padding, ' ');
    if (!entrada.read(texto.data(), len)) {
        std::cerr << "Error: fallo la lectura de '" << ruta_in << "'\n";
        return 1;
    }
    int len_total = len + padding;

    std::cout << "Texto original (" << len << " bytes, "
              << len_total << " con padding):\n";
    std::cout.write(texto.data(), len_total);
    std::cout << "\n";

    /* --- Cifrado --- */
    encrypt(llave, texto.data(), len_total);

    /* --- Escritura del binario cifrado --- */
    std::ofstream salida(ruta_out, std::ios::binary);
    if (!salida.is_open()) {
        std::cerr << "Error: no se pudo crear '" << ruta_out << "'\n";
        return 1;
    }
    salida.write(texto.data(), len_total);
    salida.close();

    std::cout << "Archivo cifrado generado: " << ruta_out
              << " (llave=" << llave << ")\n";
    return 0;
}
