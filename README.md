# Proyecto 2 — Fuerza bruta DES con MPI

Búsqueda de una llave privada DES mediante fuerza bruta, paralelizada con OpenMPI.
Curso CC3069 — Computación Paralela y Distribuida, UVG.

**Integrantes:** Angel Herrarte · Giovanni Santos · Jorge López

## Requisitos

- OpenMPI (`mpicxx`, `mpirun`)
- OpenSSL (`libcrypto`)

En macOS con Homebrew:

```bash
brew install open-mpi openssl@3
```

En Linux (Debian/Ubuntu):

```bash
sudo apt install libopenmpi-dev openssl libssl-dev
```

## Compilación

```bash
make
```

Genera tres ejecutables:

| Binario | Descripción |
|---|---|
| `cifrar` | Cifra un `.txt` con una llave dada y produce un `.bin`. |
| `bruteforce_seq` | Versión secuencial (línea base para el speedup). |
| `bruteforce_mpi` | Versión paralela con MPI (reparto `bloque` o `ciclico`). |

## Uso

### 1. Generar un texto cifrado

```bash
./cifrar entrada.txt salida.bin <llave>
```

### 2. Búsqueda secuencial (baseline)

```bash
./bruteforce_seq salida.bin <palabra_clave> [limite]
```

### 3. Búsqueda paralela

```bash
mpirun -np <N> ./bruteforce_mpi salida.bin <palabra_clave> [limite] [modo]
```

- `limite`: cota superior del espacio de llaves a probar (por defecto 2^25).
- `modo`: `bloque` (reparto contiguo ingenuo) o `ciclico` (reparto intercalado,
  speedup consistente). Por defecto `bloque`.

### Ejemplo completo

```bash
make
./cifrar prueba.txt prueba.bin 3500000
./bruteforce_seq prueba.bin jacaranda
mpirun -np 4 ./bruteforce_mpi prueba.bin jacaranda 33554432 ciclico
```

## Estrategias de reparto

- **bloque (naive):** cada proceso recibe un segmento contiguo del espacio de
  llaves. El speedup depende de dónde caiga la llave: si está en el primer
  segmento, los demás procesos no aportan (Actividad 9).
- **ciclico (mejorado):** el proceso `r` prueba las llaves `r, r+N, r+2N, ...`.
  Esto reparte la ubicación de la llave y produce un speedup consistente,
  cercano al ideal (Actividad 10).

## Comunicación MPI

- `MPI_Bcast` — el maestro (rank 0) difunde el texto cifrado y la palabra clave.
- `MPI_Irecv` — cada proceso escucha de forma no bloqueante el aviso de que la
  llave ya fue encontrada.
- `MPI_Send` — el proceso que encuentra la llave avisa a todos los demás.
- `MPI_Wait` — cierra ordenadamente la recepción pendiente.
- `MPI_Reduce` — obtiene el tiempo paralelo real (el del proceso más lento).

## Nota sobre la paridad DES

DES usa 56 bits efectivos: los 8 bits de paridad se descartan. Por eso la "llave
encontrada" puede diferir de la "llave de cifrado" en esos bits de paridad, pero
descifra el texto de forma idéntica.
