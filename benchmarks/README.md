# Medición de rendimiento

El script genera cifrados propios para tres posiciones de llave de la
actividad 9, adaptadas a un límite L: L/2+1, L/2+L/8 y ceil(L/7)+ceil(L/13).
Los nombres fácil/intermedia/difícil corresponden al reparto por bloques
con cuatro procesos; no describen dificultad absoluta ni secuencial.
La adaptación no reemplaza una aprobación académica del rango reducido.

Para cada caso se ejecuta un calentamiento por configuración y después las
repeticiones en orden aleatorio reproducible. Se verifica la llave encontrada,
se guardan salidas completas y se calcula el cociente de tiempos promedio.
No se ejecutan mediciones simultáneas ni se permite sobresuscripción.
MPI usa localhost con slots explícitos y sin afinidad fija; el planificador
del sistema decide la ubicación de los procesos. En el M2 esto puede afectar
los resultados por sus núcleos de distinto rendimiento.

```sh
export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
make
python3 benchmarks/medir.py --salida resultados/nueva-corrida --limite 4194304 --repeticiones 10 --procesos 2 4 6 8
```

Elegir una carpeta nueva para conservar corridas anteriores. El script acepta
otro texto propio mediante `--texto` y `--palabra`; vuelve a cifrarlo con las
tres llaves controladas. No sirve todavía para medir cifrados externos con
llave desconocida.

Archivos producidos:

- `entorno.json`: equipo, versiones, límite, texto, commit y hashes de binarios.
- `mediciones.csv`: tiempos internos, tiempos externos, intentos y comandos.
- `resumen.csv`: promedio, desviación estándar, extremos, speedup y eficiencia.
- `*.log`: salida completa de cada búsqueda; repetición 0 es calentamiento.
- `*.bin`: cifrados generados; se conservan los utilizados en los experimentos finales.

El speedup usa el tiempo interno de búsqueda informado por los programas;
excluye lectura, difusión inicial, impresión y arranque del lanzador MPI.
El tiempo externo registra también ese costo, pero no se mezcla con el interno.
La eficiencia es una fracción (multiplicar por 100 para porcentaje).

## Corrida ampliada

Se ejecutó el mismo procedimiento con L=4194304 (2^22), diez repeticiones
por configuración y procesos 2, 4, 6 y 8, en
`resultados/rendimiento-10-repeticiones-2026-10-14/`.
Son 270 mediciones y 27 calentamientos para tres llaves controladas del
texto propio. Las condiciones de energía y carga se registran en archivos
separados. El modo de bajo consumo estaba activado; no se cambiaron ajustes
del sistema durante la ejecución. No se controló la carga de otras aplicaciones.

El análisis está en `INFORME_RENDIMIENTO.md`, junto a los CSV y las gráficas.
Los speedups muy altos y las eficiencias mayores al 100% del reparto por
bloques deben interpretarse considerando que las versiones prueban cantidades
de llaves distintas. Los datos no reemplazan la actividad de dos textos externos.

Para generar las gráficas se requiere matplotlib:

```sh
python3 benchmarks/graficar.py resultados/rendimiento-10-repeticiones-2026-10-14
```

## Textos externos

`medir_externos.py` utiliza el archivo cifrado original y comprueba que sus
bytes no cambien durante los experimentos. Requiere una llave recuperada y
validada previamente, una frase exacta y una cota exclusiva que incluya esa
llave. No cifra de nuevo el texto. Ejemplo para el grupo 1, seleccionado para la evidencia final:

```sh
python3 benchmarks/medir_externos.py --archivo textos_otros_grupos/equipo1_mensaje.bin --grupo 1 --palabra 'quetzal sincronizado en MPI' --llave 173421 --limite 200000 --repeticiones 10 --salida resultados/nueva-corrida-grupo1
python3 benchmarks/graficar.py resultados/nueva-corrida-grupo1
```

Por texto se mide secuencial, bloques y cíclico con 2, 4, 6 y 8 procesos.
Una repetición de calentamiento por configuración se guarda como `rep=0`
y se excluye del promedio. El mínimo de la actividad 11 es diez pruebas por
texto; diez repeticiones por cada configuración proporcionan una evidencia
más extensa (90 mediciones por texto). Los CSV incluyen tiempos internos y
externos, intentos y comandos. Las publicaciones y hashes de originales se
documentan en `textos_otros_grupos/README.md`.

La evidencia final usa los grupos 1 y 7. Para validar y reunir sus CSV y
gráficas:

```sh
python3 benchmarks/resumir_externos.py resultados/otros-grupos-2026-10-14
python3 benchmarks/graficar.py resultados/otros-grupos-2026-10-14
```
