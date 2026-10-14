# Profiling

Este flujo mantiene los perfiles separados de las mediciones de rendimiento.
`make profile` genera ejecutables en `.profile-build/`, con optimización `-O2`
y símbolos `-g`. El ejecutable MPI además define `PERFIL_MPI` y enlaza
`profile_mpi.cpp`; el ejecutable normal no incorpora esa instrumentación.

```sh
export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
make profile
python3 benchmarks/perfilar.py --salida resultados/nuevo-perfil --repeticiones 3
python3 benchmarks/resumir_perfil.py resultados/nuevo-perfil
```

El muestreo CPU usa `sample` de macOS sobre el proceso secuencial durante
ocho segundos, con un intervalo solicitado de un milisegundo. La búsqueda
usa una llave al final de un rango de `2^24`, para mantenerse activa durante
el muestreo. No se agregan timers por cada candidato. El perfil atribuye
muestras a funciones; sus porcentajes no son medidas exactas de tiempo.
Funciones inline pueden aparecer atribuidas a `tryKey` en lugar de mostrar
un nombre separado para copia o búsqueda de substring.

El perfil MPI usa las tres posiciones de llave del experimento de rendimiento
en el rango de `2^22`. Para cada posición se ejecutan bloques y cíclico con
2, 4, 6 y 8 procesos y tres repeticiones, en orden aleatorio reproducible.
Se registran 72 ejecuciones y 360 filas de actividad por rank.

La ventana de medición comienza después de la barrera inicial y termina al
regresar de `buscar_en_rango`, incluyendo el `Bcast` final. Excluye lectura,
difusión inicial, arranque, impresión y recolección de los perfiles.

Cada fila contiene:

- `intentos`: candidatos probados por ese rank.
- `total_s`: tiempo local de la ventana de búsqueda.
- `busqueda_s`: suma del tiempo dentro de `buscar_lote`.
- `*_s`, `*_llamadas`: tiempo transcurrido dentro de cada llamada MPI y su cantidad.
- `otro_s`: total menos lotes y llamadas; incluye control e instrumentación.

La capa PMPI mide `Irecv`, `Test`, `Send`, `Wait`, `Waitany`, `Waitall` y
`Bcast`. No mide transferencia de red pura, bytes de tráfico ni uso de CPU
porcentaje. Una llamada MPI puede hacer comunicación, esperar o realizar
progreso interno. El tiempo acumulado de llamadas es una partición del tiempo
local, no un desglose exclusivo de comunicación versus espera.

La búsqueda y las llamadas MPI no se anidan en esta implementación. Se usa
una medición por lote, no por candidato, para limitar la perturbación.
La resolución y el costo del reloj afectan especialmente perfiles de pocos
milisegundos. Los tiempos instrumentados no sustituyen los tiempos del CSV
de rendimiento ni se usan para recalcular el speedup.

La corrida `resultados/profiling-2026-10-14/` contiene la muestra de CPU,
CSV individuales, `perfiles.csv`, un resumen y gráficas. El análisis de
resultados está en `ANALISIS_PROFILING.md`; no es el informe final del proyecto.

Para ejecutar manualmente una búsqueda instrumentada:

```sh
PROFILE_CSV=/tmp/perfil.csv mpirun --host localhost:8 --bind-to none -np 4 .profile-build/bruteforce_mpi archivo.bin jacaranda 4194304 ciclico
```

Los perfiles también se validan comparando la suma de intentos locales con
la cantidad global impresa y comprobando que todos los ranks estén presentes.
Los ejecutables instrumentados se ignoran en Git; los ciphertexts usados
en la corrida final se conservan junto a los datos.
