/* ================================================================
 * bruteforce_mpi.cpp
 * ----------------------------------------------------------------
 * Version PARALELA (OpenMPI) del ataque de fuerza bruta sobre DES.
 *
 * Esquema de comunicacion:
 *   1. El proceso maestro (rank 0) lee el archivo cifrado y la
 *      palabra clave, y los distribuye con MPI_Bcast.
 *   2. Cada proceso recorre SU porcion del espacio de llaves segun
 *      el modo de reparto elegido:
 *        - "bloque" : reparto contiguo e ingenuo (naive). El speedup
 *                     depende de donde caiga la llave (Actividad 9).
 *        - "ciclico": reparto intercalado (round-robin). Distribuye
 *                     la ubicacion de la llave para lograr un speedup
 *                     mas consistente (Actividad 10).
 *   3. Cada proceso publica un MPI_Irecv no bloqueante para escuchar
 *      si alguien ya encontro la llave. Al encontrarla, el proceso
 *      hace MPI_Send a TODOS (incluido el mismo) con tag TAG_FOUND,
 *      de modo que cada MPI_Irecv se cierra limpiamente con MPI_Wait.
 *
 * Uso:
 *   mpirun -np <N> ./bruteforce_mpi <archivo_cifrado> <palabra_clave> \
 *          [limite] [modo]
 *
 *   modo : "bloque" (por defecto) o "ciclico"
 *
 * Precondicion: la palabra clave debe ser suficientemente unica para
 * que exista UNA sola llave solucion dentro de [0, limite).
 *
 * Proyecto #2 - Computacion Paralela y Distribuida (CC3069) - UVG
 * ================================================================ */
#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include "des_utils.h"

/* Etiqueta de los mensajes que anuncian la llave encontrada. */
static const int TAG_FOUND = 1;

/* Cada cuantas llaves probadas se revisa si otro proceso ya gano.
 * Un valor pequeno reacciona rapido pero sondea mas la red; uno
 * grande reduce el sondeo pero tarda mas en detenerse. */
static const KeyType INTERVALO_SONDEO = 4096;

/* Limite por defecto del espacio de busqueda (2^25). */
static const KeyType LIMITE_POR_DEFECTO = (KeyType)1 << 25;

/* ----------------------------------------------------------------
 * Funcion: leer_archivo_cifrado
 * Proposito: (solo rank 0) Carga el contenido binario del texto
 *            cifrado y valida que sea multiplo de 8.
 * Entradas:  ruta (const string&), buffer (vector<char>&).
 * Salidas:   longitud leida, o -1 en caso de error.
 * ---------------------------------------------------------------- */
int leer_archivo_cifrado(const std::string &ruta, std::vector<char> &buffer) {
    std::ifstream archivo(ruta, std::ios::binary | std::ios::ate);
    if (!archivo.is_open()) {
        std::cerr << "Error: no se pudo abrir el archivo '" << ruta << "'\n";
        return -1;
    }
    std::streamsize tam = archivo.tellg();
    archivo.seekg(0, std::ios::beg);
    if (tam <= 0 || tam % 8 != 0) {
        std::cerr << "Error: longitud invalida (" << tam
                  << "); debe ser > 0 y multiplo de 8\n";
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
 * Funcion: buscar_en_rango
 * Proposito: Recorre la porcion de llaves asignada a este proceso,
 *            escuchando en paralelo (MPI_Irecv/MPI_Test) el aviso de
 *            que otro proceso ya encontro la solucion. Al encontrarla
 *            localmente, avisa a todos con MPI_Send.
 * Entradas:  rank, size   - identificador y total de procesos.
 *            inicio, paso - primera llave y salto entre llaves de
 *                           este proceso (define bloque o ciclico).
 *            limite       - cota superior global del espacio.
 *            ciph, len    - texto cifrado y su longitud.
 *            keyword      - frase clave buscada.
 * Salidas:   Retorna la llave solucion (conocida por todos los
 *            procesos al terminar, gracias al MPI_Send global).
 * ---------------------------------------------------------------- */
KeyType buscar_en_rango(int rank, int size, KeyType inicio, KeyType paso,
                        KeyType limite, const char *ciph, int len,
                        const char *keyword) {
    unsigned long long llave_global = 0; /* buffer del mensaje entrante */
    MPI_Request req_recv;
    MPI_Status  estado;
    int bandera = 0; /* indica si el MPI_Irecv ya se completo */

    /* Publicamos la escucha no bloqueante ANTES de empezar a buscar. */
    MPI_Irecv(&llave_global, 1, MPI_UNSIGNED_LONG_LONG, MPI_ANY_SOURCE,
              TAG_FOUND, MPI_COMM_WORLD, &req_recv);

    bool gane = false;
    KeyType contador = 0;
    for (KeyType k = inicio; k < limite; k += paso) {
        /* Cada cierto numero de intentos, revisamos si alguien gano. */
        if (++contador % INTERVALO_SONDEO == 0) {
            MPI_Test(&req_recv, &bandera, &estado);
            if (bandera) break; /* otro proceso ya encontro la llave */
        }

        if (tryKey(k, ciph, len, keyword)) {
            /* Encontramos la llave: avisamos a TODOS (incluido uno
             * mismo, para que nuestro propio MPI_Irecv se cierre). */
            unsigned long long hallada = (unsigned long long)k;
            for (int destino = 0; destino < size; destino++) {
                MPI_Send(&hallada, 1, MPI_UNSIGNED_LONG_LONG, destino,
                         TAG_FOUND, MPI_COMM_WORLD);
            }
            gane = true;
            break;
        }
    }

    /* Cierre del MPI_Irecv pendiente:
     *  - Si 'bandera' ya es true, MPI_Test lo completo: no esperamos.
     *  - Si ganamos, nuestro propio MPI_Send llega a nosotros: esperamos.
     *  - Si agotamos el rango sin ganar, esperamos al mensaje del
     *    verdadero ganador (bloquea hasta que llegue). */
    (void)gane;
    if (!bandera) {
        MPI_Wait(&req_recv, &estado);
    }

    return (KeyType)llave_global;
}

/* ----------------------------------------------------------------
 * Funcion: main
 * Proposito: Inicializa MPI, distribuye datos con MPI_Bcast, reparte
 *            el espacio de llaves, lanza la busqueda y reporta
 *            resultados y tiempo paralelo.
 * ---------------------------------------------------------------- */
int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* --- Programacion defensiva: argumentos (valida en rank 0) --- */
    if (argc < 3) {
        if (rank == 0)
            std::cerr << "Uso: mpirun -np <N> " << argv[0]
                      << " <archivo_cifrado> <palabra_clave> [limite] [modo]\n"
                      << "  modo: bloque (defecto) | ciclico\n";
        MPI_Finalize();
        return 1;
    }
    std::string ruta = argv[1];
    std::string keyword = argv[2];
    KeyType limite = (argc >= 4) ? (KeyType)std::stoull(argv[3])
                                 : LIMITE_POR_DEFECTO;
    std::string modo = (argc >= 5) ? argv[4] : "bloque";

    if (modo != "bloque" && modo != "ciclico") {
        if (rank == 0)
            std::cerr << "Error: modo invalido '" << modo
                      << "'. Use 'bloque' o 'ciclico'.\n";
        MPI_Finalize();
        return 1;
    }

    /* --- rank 0 carga el texto cifrado; comparte estado via Bcast --- */
    std::vector<char> ciph;
    int len = 0;
    if (rank == 0) {
        len = leer_archivo_cifrado(ruta, ciph);
        if (keyword.empty()) {
            std::cerr << "Error: la palabra clave no puede estar vacia\n";
            len = -1;
        }
    }

    /* Difundimos la longitud (o el codigo de error) a todos. */
    MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (len < 0) { /* error detectado en rank 0: salida ordenada */
        MPI_Finalize();
        return 1;
    }

    /* Difundimos el contenido cifrado. */
    if (rank != 0) ciph.resize(len);
    MPI_Bcast(ciph.data(), len, MPI_CHAR, 0, MPI_COMM_WORLD);

    /* Difundimos la palabra clave (longitud + contenido). */
    int kw_len = (int)keyword.size();
    MPI_Bcast(&kw_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    std::vector<char> kw_buf(kw_len + 1);
    if (rank == 0) memcpy(kw_buf.data(), keyword.data(), kw_len);
    MPI_Bcast(kw_buf.data(), kw_len, MPI_CHAR, 0, MPI_COMM_WORLD);
    kw_buf[kw_len] = '\0';

    if (rank == 0) {
        std::cout << "=== Fuerza bruta PARALELA (MPI) ===\n";
        std::cout << "Procesos        : " << size << "\n";
        std::cout << "Modo de reparto : " << modo << "\n";
        std::cout << "Archivo cifrado : " << ruta << "\n";
        std::cout << "Palabra clave   : " << kw_buf.data() << "\n";
        std::cout << "Espacio llaves  : [0, " << limite << ")\n";
        std::cout << "Longitud texto  : " << len << " bytes\n";
    }

    /* --- Definicion de la porcion de cada proceso --- *
     * bloque : inicio = rank*chunk, paso = 1 (rango contiguo).
     * ciclico: inicio = rank,       paso = size (intercalado). */
    KeyType inicio, paso, cota_local;
    if (modo == "bloque") {
        KeyType chunk = (limite + size - 1) / size; /* techo */
        inicio = (KeyType)rank * chunk;
        paso = 1;
        cota_local = inicio + chunk;
        if (cota_local > limite) cota_local = limite;
    } else { /* ciclico */
        inicio = (KeyType)rank;
        paso = (KeyType)size;
        cota_local = limite;
    }

    /* --- Busqueda con medicion de tiempo paralelo --- */
    MPI_Barrier(MPI_COMM_WORLD); /* arranque sincronizado */
    double t0 = MPI_Wtime();

    KeyType llave = buscar_en_rango(rank, size, inicio, paso, cota_local,
                                    ciph.data(), len, kw_buf.data());

    double t1 = MPI_Wtime();
    double local = t1 - t0, maximo = 0.0;
    /* El tiempo paralelo real es el del proceso mas lento. */
    MPI_Reduce(&local, &maximo, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    /* --- Despliegue de resultados (rank 0) --- */
    if (rank == 0) {
        std::vector<char> descifrado(len + 1);
        memcpy(descifrado.data(), ciph.data(), len);
        descifrado[len] = '\0';
        decrypt(llave, descifrado.data(), len);

        std::cout << "\nLlave encontrada : " << llave << "\n";
        std::cout << "Texto descifrado : " << descifrado.data() << "\n";
        std::cout << "Tiempo paralelo  : " << maximo << " segundos.\n";
    }

    MPI_Finalize();
    return 0;
}
