/* ================================================================
 * app_utils.h - Validacion, archivos, reparto y resultados compartidos.
 * CC3069 - Proyecto #2, UVG.
 * Los errores recuperables se comunican mediante std::runtime_error.
 * ================================================================ */
#ifndef APP_UTILS_H
#define APP_UTILS_H

#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "des_utils.h"

/* Entrada: decimal sin signo ni caracteres adicionales y rango inclusivo.
 * Salida: valor validado; lanza excepcion si el texto o rango es invalido. */
inline KeyType leer_numero(const std::string &texto, const char *nombre,
                           KeyType minimo, KeyType maximo) {
    KeyType valor = 0;
    auto resultado = std::from_chars(texto.data(), texto.data() + texto.size(), valor);
    if (texto.empty() || resultado.ec != std::errc{} ||
        resultado.ptr != texto.data() + texto.size() ||
        valor < minimo || valor > maximo) {
        throw std::runtime_error(std::string(nombre) + " debe ser un entero decimal entre " +
                                 std::to_string(minimo) + " y " + std::to_string(maximo));
    }
    return valor;
}

struct OpcionesBusqueda {
    std::string ruta;
    std::string keyword;
    KeyType limite = LIMITE_POR_DEFECTO;
    bool ciclico = false;
};

/* Entradas: argumentos de consola y si el programa usa MPI.
 * Salida: opciones validadas; la cota es exclusiva y debe ser > 0. */
inline OpcionesBusqueda leer_opciones(int argc, char **argv, bool paralelo) {
    if (argc < 3 || argc > (paralelo ? 5 : 4)) {
        throw std::runtime_error(std::string("Uso: ") + argv[0] +
            " <archivo_cifrado> <palabra_clave> [limite]" +
            (paralelo ? " [bloque|ciclico]" : ""));
    }
    OpcionesBusqueda op;
    op.ruta = argv[1];
    op.keyword = argv[2];
    if (op.keyword.empty() || op.keyword.size() > std::size_t(std::numeric_limits<int>::max())) {
        throw std::runtime_error("la palabra clave debe ser no vacia y caber en un mensaje MPI");
    }
    if (argc >= 4) op.limite = leer_numero(argv[3], "limite", 1, ESPACIO_DES);
    if (argc == 5) {
        const std::string modo = argv[4];
        if (modo != "bloque" && modo != "ciclico") {
            throw std::runtime_error("modo invalido: use 'bloque' o 'ciclico'");
        }
        op.ciclico = modo == "ciclico";
    }
    return op;
}

/* Entradas: ruta y si se exige un cifrado DES-ECB.
 * Salida: bytes completos. Se valida antes de reservar memoria; el tope
 * permite usar MPI con count de tipo int y agregar hasta 7 bytes de padding. */
inline std::vector<char> leer_archivo(const std::string &ruta, bool cifrado) {
    std::ifstream archivo(ruta, std::ios::binary | std::ios::ate);
    if (!archivo) throw std::runtime_error("no se pudo abrir '" + ruta + "'");
    const std::streamoff tam = archivo.tellg();
    constexpr auto maximo = std::numeric_limits<int>::max() - 7;
    if (tam <= 0 || tam > maximo || (cifrado && tam % 8 != 0)) {
        throw std::runtime_error("longitud invalida en '" + ruta +
            "': debe ser > 0, <= " + std::to_string(maximo) +
            (cifrado ? " y multiplo de 8" : ""));
    }
    std::vector<char> datos(static_cast<std::size_t>(tam));
    archivo.seekg(0, std::ios::beg);
    if (!archivo.read(datos.data(), tam)) {
        throw std::runtime_error("fallo la lectura de '" + ruta + "'");
    }
    return datos;
}

struct Rango {
    KeyType siguiente;
    KeyType fin;
    KeyType paso;
};

/* Entradas: limite exclusivo, rank valido, total de procesos y estrategia.
 * Salida: rango sin huecos ni solapamientos. Los bloques difieren a lo
 * sumo en una llave; admite mas procesos que llaves y limite = 2^56. */
inline Rango repartir(KeyType limite, int rank, int size, bool ciclico) {
    const KeyType r = static_cast<KeyType>(rank);
    const KeyType n = static_cast<KeyType>(size);
    if (ciclico) return {r, limite, n};
    const KeyType base = limite / n, extra = limite % n;
    const KeyType inicio = r * base + std::min(r, extra);
    return {inicio, inicio + base + (r < extra ? 1 : 0), 1};
}

/* Entradas: metadatos de la busqueda. Salida: descripcion en stdout. */
inline void imprimir_entrada(const OpcionesBusqueda &op, std::size_t len) {
    std::cout << "Archivo cifrado : " << op.ruta << '\n'
              << "Palabra clave   : " << op.keyword << '\n'
              << "Espacio llaves  : [0, " << op.limite << ") (indices efectivos DES)\n"
              << "Longitud texto  : " << len << " bytes\n";
}

/* Entradas: llave o SIN_LLAVE, cifrado e intentos efectivos realizados.
 * Salida: llave, bytes DES y texto completo (incluido el padding). */
inline void imprimir_resultado(KeyType llave, const std::vector<char> &ciph, KeyType intentos) {
    if (llave == SIN_LLAVE) {
        std::cout << "\nLlave NO encontrada en el rango dado.\n";
    } else {
        auto texto = ciph;
        decrypt(llave, texto.data(), texto.size());
        std::cout << "\nLlave encontrada : " << llave << '\n'
                  << "Llave DES (hex)  : " << llave_hex(llave) << '\n'
                  << "Texto descifrado : ";
        std::cout.write(texto.data(), static_cast<std::streamsize>(texto.size()));
        std::cout << '\n';
    }
    std::cout << "Llaves probadas  : " << intentos << '\n';
}

#endif
