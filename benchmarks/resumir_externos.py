#!/usr/bin/env python3
"""Verifica y reúne la evidencia final de los grupos 1 y 7."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

from medir import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('carpeta', type=Path)
    args = parser.parse_args()
    carpeta = args.carpeta
    fuentes = {f['grupo']: f for f in json.loads((carpeta / 'fuentes.json').read_text())}
    res = []
    medidas = []
    for grupo in [1, 7]:
        local = carpeta / f'grupo{grupo}'
        filas = list(csv.DictReader((local / 'mediciones.csv').open()))
        resumen = list(csv.DictReader((local / 'resumen.csv').open()))
        entorno = json.loads((local / 'entorno.json').read_text())
        repeticiones = entorno['repeticiones']
        assert repeticiones == 10
        grupos = defaultdict(list)
        texto = (carpeta / f'descifrado-grupo{grupo}.raw').read_bytes().decode('utf8')
        for f in filas:
            clave = f['modo'], int(f['procesos'])
            grupos[clave].append(f)
            assert 0 < float(f['segundos']) <= float(f['segundos_externos'])
            assert 0 < int(f['intentos']) <= int(f['limite'])
            assert int(f['llave']) == fuentes[grupo]['llave_efectiva_encontrada']
            assert int(f['limite']) == fuentes[grupo]['rango_publicado_exclusivo']
            log = (local / f"{f['modo']}-{f['procesos']}-{f['repeticion']}.log").read_text()
            actual = log.split('Texto descifrado : ', 1)[1].rsplit('\nLlaves probadas', 1)[0]
            assert actual == texto
        assert len(grupos) == 9 and len(filas) == 9 * repeticiones
        assert set(grupos) == {('secuencial', 1)} | {(modo, n) for modo in ['bloque', 'ciclico'] for n in [2, 4, 6, 8]}
        for fs in grupos.values():
            assert sorted(int(f['repeticion']) for f in fs) == list(range(1, repeticiones + 1))
        base = statistics.mean(float(f['segundos']) for f in grupos['secuencial', 1])
        for r in resumen:
            promedio = statistics.mean(float(f['segundos']) for f in grupos[r['modo'], int(r['procesos'])])
            assert math.isclose(promedio, float(r['promedio_s']))
            assert math.isclose(base / promedio, float(r['speedup']))
            assert math.isclose(base / promedio / int(r['procesos']), float(r['eficiencia']))
        archivo = ROOT / fuentes[grupo]['archivo']
        assert hashlib.sha256(archivo.read_bytes()).hexdigest() == fuentes[grupo]['sha256'] == entorno['sha256_original']
        res.extend(resumen)
        medidas.extend(filas)
        # La copia legible retira sólo el relleno reconocido; los .raw y logs lo conservan.
        (local / 'descifrado.txt').write_bytes((carpeta / f'descifrado-grupo{grupo}.txt').read_bytes())
    for nombre, filas in [('resumen.csv', res), ('mediciones.csv', medidas)]:
        with (carpeta / nombre).open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=filas[0])
            writer.writeheader()
            writer.writerows(filas)
    lines = ['# Evidencia de descifrado y rendimiento de otros grupos', '',
             'Seleccionamos los grupos **1 y 7**, usando los archivos originales y los rangos publicados en Canvas. Ambos se descifraron y las frases se verificaron también mediante OpenSSL DES-ECB independiente. Las llaves se descubrieron mediante fuerza bruta; no fueron suministradas por los autores.', '',
             '## Archivos y resultados', '',
             '| Grupo | Archivo | Bytes | Frase exacta | Rango exclusivo | Llave efectiva | DES hexadecimal |',
             '|---|---|---:|---|---|---:|---|']
    for grupo in [1, 7]:
        f = fuentes[grupo]
        lines.append(f"| {grupo} | {Path(f['archivo']).name} | {f['bytes']} | {f['palabra']} | [0, {f['rango_publicado_exclusivo']}) | {f['llave_efectiva_encontrada']} | {f['llave_des_hex']} |")
    lines += ['', '## Método', '',
              'Por texto se ejecutaron **diez repeticiones de nueve configuraciones**: secuencial y MPI con bloques/cíclico y 2, 4, 6 y 8 procesos. Esto produce **90 mediciones por texto**, 180 en total, más 18 calentamientos excluidos de los promedios. Los procesos se variaron en pasos de dos y las repeticiones se intercalaron en orden aleatorio reproducible. Cada archivo mantuvo el mismo texto, frase y rango en todas sus comparaciones.', '',
              'Se usó el mismo Apple M2 con ocho núcleos físicos, compilación `-O2`, OpenMPI 5.0.11, sin sobresuscripción y sin afinidad fija. Las configuraciones de cada texto se ejecutaron una a la vez; el grupo 1 se completó antes del grupo 7. El modo de bajo consumo estaba activado; se guardaron registros de energía y carga. No se controló la carga de otras aplicaciones.', '',
              'Speedup = promedio del tiempo secuencial / promedio del tiempo paralelo. Eficiencia = speedup / número de procesos; en la tabla se expresa como porcentaje. Se usan tiempos internos de búsqueda, excluyendo arranque MPI, lectura y difusión inicial. Los tiempos externos también se conservaron. La desviación estándar es muestral y no equivale a un intervalo de confianza.', '',
              '## Tiempos, speedup y eficiencia', '',
              '| Texto | Reparto | Procesos | Promedio (s) | Desviación (s) | Speedup | Eficiencia |',
              '|---|---|---:|---:|---:|---:|---:|']
    for r in res:
        lines.append(f"| {r['caso']} | {r['modo']} | {r['procesos']} | {float(r['promedio_s']):.6f} | {float(r['desviacion_s']):.6f} | {float(r['speedup']):.2f}× | {100 * float(r['eficiencia']):.1f}% |")
    lines += ['', '## Análisis', '',
              'Los resultados por bloques deben interpretarse junto a la posición de la llave y la cantidad de intentos. Cada cambio de procesos modifica los límites de los bloques. Una eficiencia superior al 100% indica una comparación con distinta cantidad de trabajo, no un porcentaje de utilización de CPU. El CSV conserva los intentos para sustentar este análisis.', '']
    for grupo in [1, 7]:
        sub = [r for r in res if r['caso'] == f'grupo{grupo}' and r['modo'] == 'ciclico']
        inicio = next(r for r in sub if int(r['procesos']) == 2)
        fin = next(r for r in sub if int(r['procesos']) == 8)
        lines.append(f"- **Grupo {grupo}, cíclico:** speedup de {float(inicio['speedup']):.2f}× con dos procesos a {float(fin['speedup']):.2f}× con ocho; eficiencia de {100 * float(inicio['eficiencia']):.1f}% a {100 * float(fin['eficiencia']):.1f}%. Son resultados de este archivo y este equipo.")
    lines += ['', '### Cantidad de trabajo realizado', '',
              '| Texto | Reparto | Procesos | Intentos totales promedio | Trabajo/seq |',
              '|---|---|---:|---:|---:|']
    for r in res:
        if r['modo'] == 'secuencial':
            continue
        fs = [f for f in medidas if f['caso'] == r['caso'] and f['modo'] == r['modo'] and f['procesos'] == r['procesos']]
        intentos = statistics.mean(int(f['intentos']) for f in fs)
        ratio = intentos / (int(fs[0]['llave']) + 1)
        lines.append(f"| {r['caso']} | {r['modo']} | {r['procesos']} | {intentos:.1f} | {ratio:.4f} |")
    lines += ['', 'Trabajo/seq compara intentos totales paralelos con llave + 1 (el trabajo secuencial). Puede ser menor o mayor que uno porque los ranks recorren zonas distintas y se detienen cuando llega STOP. Es evidencia necesaria para interpretar los speedups aparentes.']
    lines += ['', 'La comparación entre textos no aísla una única variable: tienen tamaños, llaves y rangos diferentes. Cada speedup usa la referencia secuencial de su propio texto. Estos experimentos cumplen el mínimo de diez pruebas por cada uno de dos textos externos y lo amplían a diez repeticiones por configuración.', '',
              '![Speedup](speedup.png)', '', '![Eficiencia](eficiencia.png)', '', '![Tiempos](promedio_s.png)', '',
              'Las gráficas de speedup y eficiencia usan escala logarítmica. Las barras de error de tiempos muestran desviación estándar. Se incluyen versiones PDF.', '',
              '## Evidencia y procedencia', '',
              '- `textos_otros_grupos/README.md`: transcripción de las publicaciones de los grupos 1 y 7 en Canvas.',
              '- `fuentes.json`: frases, rangos, llaves y SHA-256 de archivos originales.',
              '- `grupo1/` y `grupo7/`: tiempos individuales, resúmenes, 99 logs por texto, entorno y condiciones.',
              '- `descifrado-grupo1.raw` y `descifrado-grupo7.raw`: descifrado independiente con relleno original.',
              '- Copias `.txt`: texto legible; se retira el relleno de tres bytes `0x03` del grupo 1 y de tres bytes nulos del grupo 7.',
              '- `mediciones.csv` y `resumen.csv`: datos reunidos únicamente de los grupos 1 y 7.',
              '- `validacion-final.json`: integridad de repeticiones, archivos, textos y cálculos.', '',
              'El informe final se encuentra en `informe/Informe_Proyecto2_MPI.pdf`.']
    (carpeta / 'EVIDENCIA_OTROS_GRUPOS.md').write_text('\n'.join(lines) + '\n')
    val = {'grupos_finales': [1, 7], 'mediciones': len(medidas), 'configuraciones': len(res),
           'repeticiones_por_configuracion': 10, 'calentamientos': 18,
           'descifrado_igual_a_openssl': True, 'hashes_originales_sin_cambios': True,
           'repeticiones_y_calculos_verificados': True}
    (carpeta / 'validacion-final.json').write_text(json.dumps(val, indent=2) + '\n')
    print(json.dumps(val, indent=2))


if __name__ == '__main__':
    main()
