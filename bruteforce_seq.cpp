/* ================================================================
 * bruteforce_seq.cpp - Linea base secuencial para medir speedup.
 * Uso: ./bruteforce_seq <archivo.bin> <palabra> [limite]
 * Codigos de salida: 0 encontrada, 2 no encontrada, 1 error de entrada.
 * CC3069 - Proyecto #2, UVG.
 * ================================================================ */
#include <chrono>
#include "app_utils.h"

/* Valida entradas, recorre [0, limite) y mide solo la busqueda.
 * La lectura, reserva del buffer e impresion no entran en el tiempo. */
int main(int argc, char **argv) {
    try {
        const auto op = leer_opciones(argc, argv, false);
        const auto ciph = leer_archivo(op.ruta, true);
        std::vector<char> trabajo(ciph.size());
        std::cout << "=== Fuerza bruta SECUENCIAL ===\n";
        imprimir_entrada(op, ciph.size());

        KeyType llave = SIN_LLAVE, intentos = 0;
        const auto inicio = std::chrono::steady_clock::now();
        if (op.keyword.size() <= ciph.size()) {
            for (KeyType k = 0; k < op.limite; ++k) {
                ++intentos;
                if (tryKey(k, ciph, op.keyword, trabajo)) {
                    llave = k;
                    break;
                }
            }
        }
        const double segundos = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - inicio).count();

        imprimir_resultado(llave, ciph, intentos);
        std::cout << "Tiempo de busqueda: " << segundos << " segundos.\n";
        return llave == SIN_LLAVE ? 2 : 0;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
