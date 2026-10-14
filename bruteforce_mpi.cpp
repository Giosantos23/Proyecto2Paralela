/* ================================================================
 * bruteforce_mpi.cpp - Fuerza bruta DES con coordinacion en rank 0.
 * Uso: mpirun -np N ./bruteforce_mpi archivo.bin palabra [limite] [modo]
 * Todos los ranks buscan. Cada trabajador envia UN reporte al maestro,
 * incluso al agotar su rango o recibir STOP; el maestro recibe todos.
 * No se presupone que exista una solucion ni que esta sea unica.
 * CC3069 - Proyecto #2, UVG.
 * ================================================================ */
#include <mpi.h>
#include <array>
#include "app_utils.h"
#ifdef PERFIL_MPI
#include "benchmarks/profile_mpi.h"
KeyType intentos_perfil = 0;
#endif

constexpr int TAG_RESULTADO = 1;
constexpr int TAG_STOP = 2;
constexpr KeyType TAM_LOTE = 1024;
// Elementos: llave candidata (o SIN_LLAVE), numero de intentos locales.
using Reporte = std::array<KeyType, 2>;

/* Entradas: rango mutable, datos de busqueda y buffer reutilizable.
 * Salida: actualiza cursor, candidato e intentos; trabaja como maximo
 * TAM_LOTE llaves para permitir atender MPI entre lotes. */
void buscar_lote(Rango &rango, const std::vector<char> &ciph,
                  const std::string &keyword, std::vector<char> &trabajo,
                  Reporte &reporte) {
    #ifdef PERFIL_MPI
    const double inicio_lote = MPI_Wtime();
    const KeyType antes = reporte[1];
    #endif
    for (KeyType i = 0; i < TAM_LOTE && rango.siguiente < rango.fin; ++i) {
        const KeyType k = rango.siguiente;
        rango.siguiente += rango.paso; // <= 2^56 + size: no desborda uint64_t.
        ++reporte[1];
        if (tryKey(k, ciph, keyword, trabajo)) {
            reporte[0] = k;
            break;
        }
    }
    #ifdef PERFIL_MPI
    perfil_lote(MPI_Wtime() - inicio_lote);
    intentos_perfil += reporte[1] - antes;
    #endif
}

/* Entradas: identificadores MPI, rango propio, cifrado, palabra y buffer.
 * Salida: el mismo reporte global en todos los ranks.
 * Invariantes: una recepcion por reporte y una por STOP, publicadas antes
 * de buscar. Rank 0 nunca se envia a si mismo. Todos los requests se
 * completan antes del Bcast final, sin depender del buffering de Send. */
Reporte buscar_en_rango(int rank, int size, Rango rango,
                        const std::vector<char> &ciph, const std::string &keyword,
                        std::vector<char> &trabajo) {
    Reporte resultado{SIN_LLAVE, 0};
    // Una palabra mayor que el texto no puede aparecer en ningun descifrado.
    if (keyword.size() > ciph.size()) rango.siguiente = rango.fin;

    if (rank != 0) {
        int stop = 0, completado = 0;
        MPI_Request recepcion_stop;
        MPI_Irecv(&stop, 1, MPI_INT, 0, TAG_STOP, MPI_COMM_WORLD, &recepcion_stop);
        while (rango.siguiente < rango.fin && resultado[0] == SIN_LLAVE) {
            MPI_Test(&recepcion_stop, &completado, MPI_STATUS_IGNORE);
            if (completado) break;
            buscar_lote(rango, ciph, keyword, trabajo, resultado);
        }
        // El maestro ya tiene una recepcion especifica para este rank.
        MPI_Send(resultado.data(), 2, MPI_UINT64_T, 0, TAG_RESULTADO, MPI_COMM_WORLD);
        // Tambien los trabajadores agotados reciben STOP. Si Test completo
        // el request, Wait acepta MPI_REQUEST_NULL y retorna inmediatamente.
        MPI_Wait(&recepcion_stop, MPI_STATUS_IGNORE);
    } else {
        std::vector<Reporte> reportes(size, Reporte{SIN_LLAVE, 0});
        std::vector<MPI_Request> pendientes(size, MPI_REQUEST_NULL);
        for (int r = 1; r < size; ++r) {
            MPI_Irecv(reportes[r].data(), 2, MPI_UINT64_T, r, TAG_RESULTADO,
                      MPI_COMM_WORLD, &pendientes[r]);
        }
        int restantes = size - 1;
        KeyType candidata = SIN_LLAVE;
        while (candidata == SIN_LLAVE) {
            for (int r = 1; r < size; ++r) {
                if (pendientes[r] == MPI_REQUEST_NULL) continue;
                int listo = 0;
                MPI_Test(&pendientes[r], &listo, MPI_STATUS_IGNORE);
                if (listo) {
                    --restantes;
                    candidata = std::min(candidata, reportes[r][0]);
                }
            }
            if (candidata != SIN_LLAVE) break;
            if (rango.siguiente < rango.fin) {
                buscar_lote(rango, ciph, keyword, trabajo, reportes[0]);
                candidata = reportes[0][0];
            } else if (restantes > 0) {
                // Sin trabajo local, espera un reporte en lugar de ocupar CPU.
                int indice;
                MPI_Waitany(size, pendientes.data(), &indice, MPI_STATUS_IGNORE);
                --restantes;
                candidata = reportes[indice][0];
            } else {
                break; // Todos agotaron sus rangos sin solucion.
            }
        }
        // STOP tiene un receptor por trabajador, aun si este ya reporto.
        // Los reportes concurrentes siguen teniendo sus Irecv pendientes.
        int stop = 1;
        for (int r = 1; r < size; ++r) {
            MPI_Send(&stop, 1, MPI_INT, r, TAG_STOP, MPI_COMM_WORLD);
        }
        MPI_Waitall(size, pendientes.data(), MPI_STATUSES_IGNORE);
        for (const auto &reporte : reportes) {
            resultado[0] = std::min(resultado[0], reporte[0]);
            resultado[1] += reporte[1];
        }
    }
    // Incluye hallazgos simultaneos ya recibidos. No promete la menor llave
    // del espacio, sino una llave valida y el mismo resultado para todos.
    MPI_Bcast(resultado.data(), 2, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    return resultado;
}

/* Rank 0 valida y lee una sola vez; transmite estado, opciones y datos.
 * Los errores de entrada finalizan ordenadamente. Una excepcion inesperada
 * en cualquier rank aborta el comunicador para no dejar pares esperando. */
int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    try {
        OpcionesBusqueda op;
        std::vector<char> ciph;
        int valido = 1;
        if (rank == 0) {
            try {
                op = leer_opciones(argc, argv, true);
                ciph = leer_archivo(op.ruta, true);
            } catch (const std::exception &error) {
                std::cerr << "Error: " << error.what() << '\n';
                valido = 0;
            }
        }
        MPI_Bcast(&valido, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (!valido) {
            MPI_Finalize();
            return 1;
        }
        int len = static_cast<int>(ciph.size());
        int kw_len = static_cast<int>(op.keyword.size());
        int ciclico = op.ciclico ? 1 : 0;
        MPI_Bcast(&op.limite, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
        MPI_Bcast(&ciclico, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&kw_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
        op.ciclico = ciclico != 0;
        if (rank != 0) {
            ciph.resize(len);
            op.keyword.resize(kw_len);
        }
        MPI_Bcast(ciph.data(), len, MPI_CHAR, 0, MPI_COMM_WORLD);
        MPI_Bcast(op.keyword.data(), kw_len, MPI_CHAR, 0, MPI_COMM_WORLD);
        std::vector<char> trabajo(ciph.size());
        const auto rango = repartir(op.limite, rank, size, op.ciclico);
        if (rank == 0) {
            std::cout << "=== Fuerza bruta PARALELA (MPI) ===\n"
                      << "Procesos        : " << size << '\n'
                      << "Modo de reparto : " << (op.ciclico ? "ciclico" : "bloque") << '\n';
            imprimir_entrada(op, ciph.size());
        }

        MPI_Barrier(MPI_COMM_WORLD);
        const double t0 = MPI_Wtime();
        #ifdef PERFIL_MPI
        perfil_iniciar();
        #endif
        const auto resultado = buscar_en_rango(rank, size, rango, ciph, op.keyword, trabajo);
        const double local = MPI_Wtime() - t0;
        #ifdef PERFIL_MPI
        perfil_finalizar(rank, size, intentos_perfil, local, op.ciclico);
        #endif
        double maximo = 0;
        MPI_Reduce(&local, &maximo, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        if (rank == 0) {
            imprimir_resultado(resultado[0], ciph, resultado[1]);
            std::cout << "Tiempo paralelo  : " << maximo << " segundos.\n";
        }
        MPI_Finalize();
        return resultado[0] == SIN_LLAVE ? 2 : 0;
    } catch (const std::exception &error) {
        std::cerr << "Error en rank " << rank << ": " << error.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
}
