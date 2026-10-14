/* ================================================================
 * bruteforce_seq.cpp
 * ----------------------------------------------------------------
 * Version SECUENCIAL del ataque de fuerza bruta sobre DES. Recorre
 * el espacio de llaves [0, limite) en un solo proceso. Sirve como
 * linea base (T_secuencial) para calcular el speedup de la version
 * paralela:   speedup = T_secuencial / T_paralelo.
 *
 * Uso:
 *   ./bruteforce_seq <archivo_cifrado> <palabra_clave> [limite]
 *
 *   archivo_cifrado : ruta al .bin producido por ./cifrar
 *   palabra_clave   : substring que debe aparecer al descifrar
 *   limite          : cota superior del espacio de llaves a probar
 *                     (opcional; por defecto 2^25)
 *
 * Proyecto #2 - Computacion Paralela y Distribuida (CC3069) - UVG
 * ================================================================ */
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include "des_utils.h"

/* Limite por defecto del espacio de busqueda (2^25 ~= 33.5 millones).
 * Para los experimentos de ubicacion de llave (Actividad 9) se elige
 * el valor de la llave como fraccion de este limite. */
static const KeyType LIMITE_POR_DEFECTO = (KeyType)1 << 25;

/* ----------------------------------------------------------------
 * Funcion: leer_archivo_cifrado
 * Proposito: Carga en memoria el contenido binario del texto cifrado.
 * Entradas:  ruta (const string&) - ubicacion del archivo .bin.
 *            buffer (vector<char>&) - destino de los bytes leidos.
 * Salidas:   Retorna la longitud leida, o -1 si hubo error o si la
 *            longitud no es multiplo de 8 (requisito de DES-ECB).
 * ---------------------------------------------------------------- */
int leer_archivo_cifrado(const std::string &ruta, std::vector<char> &buffer) {
    std::ifstream archivo(ruta, std::ios::binary | std::ios::ate);
    if (!archivo.is_open()) {
        std::cerr << "Error: no se pudo abrir el archivo '" << ruta << "'\n";
        return -1;
    }
    std::streamsize tam = archivo.tellg();
    archivo.seekg(0, std::ios::beg);
    if (tam <= 0) {
        std::cerr << "Error: el archivo '" << ruta << "' esta vacio\n";
        return -1;
    }
    if (tam % 8 != 0) {
        std::cerr << "Error: la longitud (" << tam << ") no es multiplo de 8\n";
        return -1;
    }
    buffer.resize(tam);
    if (!archivo.read(buffer.data(), tam)) {
        std::cerr << "Error: fallo la lectura de '" << ruta << "'\n";
        return -1;
    }
    return (int)tam;
}

/* ----------------------------------------------------------------
 * Funcion: main
 * Proposito: Punto de entrada. Valida argumentos (programacion
 *            defensiva), carga el texto cifrado, recorre el espacio
 *            de llaves de forma secuencial y reporta llave, texto
 *            descifrado y tiempo de busqueda.
 * ---------------------------------------------------------------- */
int main(int argc, char **argv) {
    /* --- Programacion defensiva: validacion de argumentos --- */
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0]
                  << " <archivo_cifrado> <palabra_clave> [limite]\n";
        return 1;
    }
    std::string ruta = argv[1];
    std::string keyword = argv[2];
    KeyType limite = (argc >= 4) ? (KeyType)std::stoull(argv[3])
                                 : LIMITE_POR_DEFECTO;

    if (keyword.empty()) {
        std::cerr << "Error: la palabra clave no puede estar vacia\n";
        return 1;
    }

    /* --- Carga del texto cifrado --- */
    std::vector<char> ciph;
    int len = leer_archivo_cifrado(ruta, ciph);
    if (len < 0) return 1;

    std::cout << "=== Fuerza bruta SECUENCIAL ===\n";
    std::cout << "Archivo cifrado : " << ruta << "\n";
    std::cout << "Palabra clave   : " << keyword << "\n";
    std::cout << "Espacio llaves  : [0, " << limite << ")\n";
    std::cout << "Longitud texto  : " << len << " bytes\n";

    /* --- Busqueda secuencial con medicion de tiempo --- */
    auto inicio = std::chrono::high_resolution_clock::now();

    KeyType llave_encontrada = 0;
    bool encontrada = false;
    for (KeyType k = 0; k < limite; k++) {
        if (tryKey(k, ciph.data(), len, keyword.c_str())) {
            llave_encontrada = k;
            encontrada = true;
            break;
        }
    }

    auto fin = std::chrono::high_resolution_clock::now();
    double segundos = std::chrono::duration<double>(fin - inicio).count();

    /* --- Despliegue de resultados --- */
    if (encontrada) {
        std::vector<char> descifrado(len + 1);
        memcpy(descifrado.data(), ciph.data(), len);
        descifrado[len] = '\0';
        decrypt(llave_encontrada, descifrado.data(), len);

        std::cout << "\nLlave encontrada : " << llave_encontrada << "\n";
        std::cout << "Texto descifrado : " << descifrado.data() << "\n";
    } else {
        std::cout << "\nLlave NO encontrada en el rango dado.\n";
    }
    std::cout << "Tiempo de busqueda: " << segundos << " segundos.\n";

    return 0;
}
