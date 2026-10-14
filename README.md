# Proyecto 2 — Fuerza bruta DES con MPI

Búsqueda de una llave DES mediante fuerza bruta, con versiones secuencial y
paralela en C++17 y OpenMPI. Curso CC3069, UVG.

**Integrantes:** Angel Herrarte · Giovanni Santos · Jorge López

## Compilación

Requiere un compilador C++17, OpenMPI y OpenSSL (`libcrypto`). Las pruebas
requieren además Python 3, sin paquetes adicionales.

```bash
# macOS / Homebrew
brew install open-mpi openssl@3

# Debian / Ubuntu
sudo apt install g++ make openmpi-bin libopenmpi-dev libssl-dev python3

make
```

En instalaciones no estándar se puede indicar el prefijo de OpenSSL:
`make OPENSSL=/ruta/openssl`.

| Archivo | Propósito |
|---|---|
| `cifrar.cpp` | Lee un texto de hasta 350 palabras y genera un `.bin` DES-ECB. |
| `bruteforce_seq.cpp` | Busca en un proceso y mide el tiempo secuencial. |
| `bruteforce_mpi.cpp` | Busca con reparto por bloques o cíclico y terminación coordinada. |
| `des_utils.h` | Representación de llaves, cifrado, descifrado y prueba de candidatos. |
| `app_utils.h` | Validación, lectura de archivos, reparto y presentación de resultados. |
| `tests/` | Regresiones funcionales, criptográficas y de comunicación. |
| `bruteforce.cpp` | Prototipo histórico de Gio; no forma parte de la compilación ni de las mediciones finales. |

## Uso

```bash
./cifrar <entrada.txt> <salida.bin> <llave> [--legacy]
./bruteforce_seq <salida.bin> <palabra_clave> [limite]
mpirun -np <N> ./bruteforce_mpi <salida.bin> <palabra_clave> [limite] [modo]
```

- `llave`: índice efectivo entre `0` y `72057594037927935` (`2^56 - 1`).
- `limite`: cota **exclusiva**, entre `1` y `72057594037927936` (`2^56`).
  El valor por defecto es `33554432` (`2^25`) para experimentos manejables.
  Ese valor es un subconjunto del espacio completo. Una llave igual o mayor
  que el límite no se prueba.
- `modo`: `bloque` (por defecto) o `ciclico`. Para indicar el modo también
  debe indicarse el límite.
- La palabra o frase debe aparecer exactamente, respetando mayúsculas,
  acentos y codificación. Usar comillas para frases con espacios y elegir una
  frase suficientemente distintiva para reducir coincidencias accidentales.
- Los argumentos numéricos son decimales sin signo; se rechazan sufijos,
  espacios, desbordamientos y valores fuera de rango.

Ejemplo completo:

```bash
make
./cifrar prueba.txt prueba.bin 3500000
./bruteforce_seq prueba.bin jacaranda 33554432
mpirun -np 4 ./bruteforce_mpi prueba.bin jacaranda 33554432 bloque
mpirun -np 4 ./bruteforce_mpi prueba.bin jacaranda 33554432 ciclico
```

Ambas búsquedas imprimen archivo, palabra, rango, llave candidata, sus ocho
bytes DES en hexadecimal, texto descifrado, cantidad de llaves probadas y
tiempo. El cifrado usa bloques de ocho bytes y añade espacios al final cuando
hace falta; el formato no almacena la longitud original, por lo que esos
espacios se conservan al mostrar el descifrado.

| Código de salida | Significado |
|---|---|
| `0` | Cifrado generado o llave candidata encontrada. |
| `1` | Error de argumentos, archivos o ejecución. |
| `2` | Búsqueda completada sin encontrar la palabra dentro del rango. |

`mpirun` puede añadir un diagnóstico cuando los procesos retornan `1` o `2`;
la salida `2` es un resultado normal de búsqueda, no un bloqueo. Los archivos
cifrados deben ser no vacíos y múltiplos de ocho bytes. Se rechazan tamaños
superiores a `INT_MAX - 7` antes de reservar memoria, para evitar desbordar
los contadores de MPI. El cifrador cuenta palabras separadas por espacios,
saltos de línea o tabulaciones y rechaza más de 350.

## Llaves de 56 bits y compatibilidad

El índice efectivo se divide en ocho grupos de siete bits. El grupo menos
significativo se coloca en el primer byte DES, desplazado un bit para reservar
la paridad. Así se obtiene una correspondencia uno a uno entre los `2^56`
índices y las llaves DES, independiente del endianness del equipo.

El programa anterior copiaba directamente un entero de 64 bits y reemplazaba
sus bits de paridad. Esto generaba duplicados y usaba otra numeración. Por
ejemplo, las llaves antiguas `42` y `43` equivalen al índice efectivo `21`;
las **nuevas** llaves `42` y `43` son diferentes.

Los `.bin` anteriores siguen siendo DES-ECB y se pueden buscar con los nuevos
programas **sin convertir el archivo**. La llave encontrada se imprime con
la numeración efectiva nueva. Para reproducir un cifrado anterior usando su
número de llave original, se ofrece `--legacy`:

```bash
./cifrar entrada.txt antiguo.bin 173800 --legacy
./bruteforce_seq antiguo.bin palabra_del_texto 32768
# La llave efectiva correspondiente es 27124.
```

`--legacy` interpreta el entero como los ocho bytes little-endian usados por
el prototipo en macOS y Linux x86; admite valores de `0` a `2^64 - 1` y muestra
la conversión a índice efectivo. Las búsquedas siempre recorren índices
efectivos, incluso para archivos anteriores. Al compartir con otro grupo,
confirmar DES-ECB, relleno, palabra/codificación y representación de la llave;
la salida hexadecimal permite comparar los bytes reales.

## Reparto y comunicación MPI

- **Bloques:** divide `[0, limite)` en segmentos contiguos, con tamaños que
  difieren a lo sumo en uno. Los primeros ranks reciben las llaves sobrantes.
  La ubicación de la solución puede producir speedups aparentes muy altos o
  muy bajos, como pide analizar la actividad 9.
- **Cíclico:** el rank `r` prueba `r, r+N, r+2N, ...`. Distribuye entre procesos
  el recorrido incremental del secuencial y reduce su dependencia de los
  límites de los bloques. La mejora real debe comprobarse con mediciones;
  no se garantiza un speedup ideal, especialmente para búsquedas pequeñas.

Ambos modos cubren el rango sin huecos ni duplicados, incluso si hay más
procesos que llaves. Rank 0 también busca, además de coordinar:

1. Rank 0 valida argumentos y archivo y difunde el estado con `MPI_Bcast`.
   Ante una entrada inválida, todos finalizan ordenadamente. Si es válida,
   difunde límite, modo, texto cifrado y palabra.
2. Rank 0 publica un `MPI_Irecv` **por trabajador** para recibir su reporte.
   Cada trabajador publica un `MPI_Irecv` para recibir `STOP` del maestro.
3. Los procesos buscan en lotes de hasta 1024 candidatos y atienden MPI entre
   lotes mediante `MPI_Test`. Cada trabajador envía exactamente un reporte
   con `MPI_Send`: candidato o `SIN_LLAVE`, más sus intentos. Reporta al
   encontrar una llave, agotar el rango o recibir `STOP`.
4. Si rank 0 agota su propio rango, espera reportes con `MPI_Waitany`.
   Cuando encuentra o recibe una solución, o todos agotaron sus rangos,
   envía un único `STOP` a cada trabajador, incluidos quienes ya reportaron.
5. Cada trabajador completa su recepción con `MPI_Wait`; el maestro completa
   todos los reportes con `MPI_Waitall`, incluidos hallazgos simultáneos.
   Se difunde el resultado común con `MPI_Bcast` y se finaliza con
   `MPI_Finalize`. No hay envíos a uno mismo ni recepciones abandonadas.

Si varias llaves producen la palabra, se devuelve la menor de las candidatas
reportadas, **no necesariamente la menor de todo el espacio**. La búsqueda
se detiene al obtener una solución; todos los ranks reciben el mismo
resultado. No hace falta asumir que existe un único ganador.

El protocolo tiene una recepción correspondiente para cada envío y se prueba
además reemplazando `MPI_Send` por `MPI_Ssend`, conforme al criterio de
[independencia del buffering del estándar MPI](https://www.mpi-forum.org/docs/mpi-3.1/mpi31-report/node58.htm).
Una excepción inesperada durante la ejecución llama a `MPI_Abort` para no
dejar otros procesos esperando; los errores de entrada normales usan la
salida coordinada descrita anteriormente.

## Tiempos y trabajo pendiente de experimentación

La versión secuencial usa `steady_clock`; la paralela sincroniza el inicio
con `MPI_Barrier`, mide con `MPI_Wtime` y usa `MPI_Reduce` con `MPI_MAX` para
reportar el tiempo del proceso más lento. Incluye búsqueda y protocolo de
terminación; excluye lectura, difusión inicial, impresión y arranque de
`mpirun`. Ambas versiones reutilizan un buffer de descifrado.

Para completar las actividades 7, 9, 10 y 11:

- Mantener el mismo texto, palabra, límite, versión y representación de llave
  al comparar secuencial, bloques y cíclico. Usar llaves efectivas y registrar
  su posición relativa en el rango, además de los intentos realizados.
- Realizar las repeticiones requeridas y promediar. Calcular
  `speedup = promedio(Tseq) / promedio(Tpar)` y `eficiencia = speedup / N`.
- Probar al menos dos textos de otros grupos, con un mínimo de diez pruebas
  por texto, variando el número de procesos en incrementos/decrementos de dos.
- Registrar equipo, compilador, versiones de MPI/OpenSSL, procesos, tiempos,
  profiling y condiciones de ejecución; elaborar tablas, gráficas y bitácora.
- Actualizar el informe, diagramas y catálogo de funciones con el protocolo
  actual, y completar conclusiones, referencias y entrega del código fuente.

Las pruebas automáticas siguientes verifican corrección. No sustituyen esa
bitácora ni demuestran consistencia del speedup. El ensayo de `2^56` valida
la cota con una solución temprana; no recorre exhaustivamente ese espacio.

## Pruebas reproducibles

```bash
make test
make test-sync
```

`make test` comprueba un vector conocido de DES, la paridad, los 56 bits,
65 536 índices sin duplicados, compatibilidad con un cifrado de `b7ac31a` y
repartos completos. La suite de integración ejecuta ambos modos con 1, 2, 3,
4 y 6 procesos, llaves en los extremos, rangos desiguales y vacíos por rank,
ausencia de solución, varios candidatos, argumentos inválidos, archivos
malformados/demasiado grandes, texto de 350 palabras y palabras que cruzan
bloques o aparecen después de un byte nulo.

`make test-sync` repite la integración con un binario de prueba que exige
recepciones correspondientes para cada envío. Todos los lanzamientos tienen
un timeout que detecta bloqueos y termina los procesos de esa prueba.

Por defecto las pruebas se ejecutan solamente en `localhost` y permiten
sobresuscripción para probar más procesos que núcleos disponibles. Esto es
para validación funcional; evitar sobresuscripción en mediciones de speedup.
Se pueden ajustar `MPIEXEC`, `MPIEXEC_FLAGS`, `MPI_BINARY` y `PYTHON`:

```bash
MPIEXEC=mpiexec MPIEXEC_FLAGS='--host localhost --oversubscribe' make test
```

`make clean` elimina ejecutables y `.test-build/`; las pruebas usan archivos
temporales y no modifican los textos del equipo.
