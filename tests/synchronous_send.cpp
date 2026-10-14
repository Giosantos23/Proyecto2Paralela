/* Instrumentacion de prueba: exige receptor correspondiente para cada envio.
 * Solo se enlaza en el ejecutable de make test-sync; no cambia produccion.
 * El protocolo debe funcionar tambien sin buffering de MPI_Send. */
#include <mpi.h>
extern "C" int MPI_Send(const void *buf, int count, MPI_Datatype datatype,
                         int dest, int tag, MPI_Comm comm) {
    return PMPI_Ssend(buf, count, datatype, dest, tag, comm);
}
