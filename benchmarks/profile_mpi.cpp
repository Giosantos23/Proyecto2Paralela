// Instrumentación PMPI: tiempo dentro de llamadas, no tráfico de red puro.
#include <mpi.h>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include "profile_mpi.h"

namespace {
bool activo = false;
double lotes = 0;
enum Tipo { IRECV, TEST, SEND, WAIT, WAITANY, WAITALL, BCAST, CANTIDAD };
std::array<double, CANTIDAD> tiempos{};
std::array<unsigned long long, CANTIDAD> llamadas{};
struct Medir {
    Tipo tipo;
    bool registrar;
    double inicio;
    explicit Medir(Tipo t) : tipo(t), registrar(activo), inicio(registrar ? PMPI_Wtime() : 0) {}
    ~Medir() {
        if (registrar) {
            tiempos[tipo] += PMPI_Wtime() - inicio;
            ++llamadas[tipo];
        }
    }
};
}

void perfil_iniciar() { activo = true; }
void perfil_lote(double segundos) { lotes += segundos; }

void perfil_finalizar(int rank, int size, unsigned long long intentos,
                      double total, bool ciclico) {
    activo = false;
    std::array<double, CANTIDAD + 2> local{};
    local[0] = total;
    local[1] = lotes;
    for (int i = 0; i < CANTIDAD; ++i) local[i + 2] = tiempos[i];
    std::array<unsigned long long, CANTIDAD + 1> conteos{};
    conteos[0] = intentos;
    for (int i = 0; i < CANTIDAD; ++i) conteos[i + 1] = llamadas[i];
    std::vector<double> todos(size * local.size());
    std::vector<unsigned long long> cantidades(size * conteos.size());
    PMPI_Gather(local.data(), local.size(), MPI_DOUBLE, todos.data(), local.size(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
    PMPI_Gather(conteos.data(), conteos.size(), MPI_UNSIGNED_LONG_LONG, cantidades.data(), conteos.size(), MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        const char *ruta = std::getenv("PROFILE_CSV");
        if (!ruta) throw std::runtime_error("Falta PROFILE_CSV para guardar el perfil");
        std::ofstream archivo(ruta);
        if (!archivo) throw std::runtime_error("No se pudo crear PROFILE_CSV");
        archivo << "rank,procesos,modo,intentos,total_s,busqueda_s,otro_s";
        const char *nombres[] = {"irecv", "test", "send", "wait", "waitany", "waitall", "bcast"};
        for (auto nombre : nombres) archivo << ',' << nombre << "_s," << nombre << "_llamadas";
        archivo << '\n' << std::setprecision(12);
        for (int r = 0; r < size; ++r) {
            const double *datos = todos.data() + r * local.size();
            const auto *cuentas = cantidades.data() + r * conteos.size();
            double mpi = 0;
            for (int i = 0; i < CANTIDAD; ++i) mpi += datos[i + 2];
            archivo << r << ',' << size << ',' << (ciclico ? "ciclico" : "bloque")
                    << ',' << cuentas[0] << ',' << datos[0] << ',' << datos[1]
                    << ',' << datos[0] - datos[1] - mpi;
            for (int i = 0; i < CANTIDAD; ++i) archivo << ',' << datos[i + 2] << ',' << cuentas[i + 1];
            archivo << '\n';
        }
        archivo.close();
        if (!archivo) throw std::runtime_error("Falló escritura del perfil");
    }
}

extern "C" {
int MPI_Irecv(void *b, int c, MPI_Datatype d, int s, int t, MPI_Comm m, MPI_Request *r) {
    Medir medir(IRECV); return PMPI_Irecv(b,c,d,s,t,m,r);
}
int MPI_Test(MPI_Request *r, int *f, MPI_Status *s) {
    Medir medir(TEST); return PMPI_Test(r,f,s);
}
int MPI_Send(const void *b, int c, MPI_Datatype d, int s, int t, MPI_Comm m) {
    Medir medir(SEND); return PMPI_Send(b,c,d,s,t,m);
}
int MPI_Wait(MPI_Request *r, MPI_Status *s) {
    Medir medir(WAIT); return PMPI_Wait(r,s);
}
int MPI_Waitany(int c, MPI_Request *r, int *i, MPI_Status *s) {
    Medir medir(WAITANY); return PMPI_Waitany(c,r,i,s);
}
int MPI_Waitall(int c, MPI_Request *r, MPI_Status *s) {
    Medir medir(WAITALL); return PMPI_Waitall(c,r,s);
}
int MPI_Bcast(void *b, int c, MPI_Datatype d, int r, MPI_Comm m) {
    Medir medir(BCAST); return PMPI_Bcast(b,c,d,r,m);
}
}
