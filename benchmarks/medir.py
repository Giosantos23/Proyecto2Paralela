#!/usr/bin/env python3
"""Mide las versiones sin incluir el arranque de MPI en el speedup."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import shlex
import signal
import statistics
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def ejecutar(comando, timeout):
    inicio = time.perf_counter()
    proceso = subprocess.Popen(list(map(str, comando)), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, start_new_session=True)
    try:
        salida = proceso.communicate(timeout=timeout)[0].decode(errors='replace')
    except subprocess.TimeoutExpired:
        os.killpg(proceso.pid, signal.SIGKILL)
        proceso.communicate()
        raise RuntimeError(f'Timeout: {shlex.join(list(map(str, comando)))}')
    if proceso.returncode:
        raise RuntimeError(f'Error {proceso.returncode}: {salida}')
    return salida, time.perf_counter() - inicio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--limite', type=int, default=262144)
    parser.add_argument('--repeticiones', type=int, default=3)
    parser.add_argument('--procesos', type=int, nargs='+', default=[2, 4])
    parser.add_argument('--texto', type=Path, default=ROOT / 'prueba.txt')
    parser.add_argument('--palabra', default='jacaranda')
    parser.add_argument('--salida', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=300)
    args = parser.parse_args()
    if not 16 <= args.limite <= 2**56 or args.repeticiones < 1 or any(n < 1 for n in args.procesos):
        parser.error('Límite >=16 y <=2^56; repeticiones y procesos positivos.')
    texto = args.texto.resolve()
    if args.palabra.encode() not in texto.read_bytes():
        parser.error('La palabra debe estar presente en el texto.')
    args.salida.mkdir(parents=True, exist_ok=False)
    salida_dir = args.salida.resolve()
    def version(cmd):
        try:
            return ejecutar(cmd, 15)[0].strip()
        except (RuntimeError, OSError) as error:
            return str(error)
    metadata = {
        'fecha_utc': datetime.now(timezone.utc).isoformat(),
        'plataforma': platform.platform(), 'arquitectura': platform.machine(),
        'cpu': version(['/usr/sbin/sysctl', '-n', 'machdep.cpu.brand_string']),
        'nucleos_fisicos': version(['/usr/sbin/sysctl', '-n', 'hw.physicalcpu']),
        'nucleos_logicos': os.cpu_count(),
        'mpi': version(['mpirun', '--version']),
        'compilador': version(['mpicxx', '--version']),
        'openssl': version(['/opt/homebrew/opt/openssl@3/bin/openssl', 'version']),
        'git_commit': version(['git', '-C', ROOT, 'rev-parse', 'HEAD']),
        'git_estado': version(['git', '-C', ROOT, 'status', '--short']),
        'makefile': (ROOT / 'Makefile').read_text(),
        'sha256_binarios': {nombre: hashlib.sha256((ROOT / nombre).read_bytes()).hexdigest()
                           for nombre in ['cifrar', 'bruteforce_seq', 'bruteforce_mpi']},
        'texto': str(texto), 'texto_sha256': hashlib.sha256(texto.read_bytes()).hexdigest(),
        'palabra': args.palabra, 'limite': args.limite,
        'repeticiones': args.repeticiones, 'procesos': args.procesos,
        'semilla_orden': 3069, 'calentamientos_por_configuracion': 1,
        'nota': 'Casos adaptados al rango reducido; no recorre 2^56. Tiempo interno para speedup; tiempo externo incluye lanzamiento.',
    }
    fisicos = metadata['nucleos_fisicos']
    slots = int(fisicos) if fisicos.isdecimal() else os.cpu_count()
    if slots is None or max(args.procesos) > slots:
        parser.error('Los procesos no deben exceder los núcleos físicos disponibles.')
    (salida_dir / 'entorno.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False))
    casos = {'facil': args.limite // 2 + 1,
             'intermedia': args.limite // 2 + args.limite // 8,
             'dificil': (args.limite + 6) // 7 + (args.limite + 12) // 13}
    configs = [('secuencial', 1)] + [(modo, n) for n in args.procesos for modo in ['bloque', 'ciclico']]
    generador = random.Random(3069)
    filas = []
    campos = ['caso', 'llave', 'limite', 'modo', 'procesos', 'repeticion', 'segundos', 'segundos_externos', 'intentos', 'comando']
    with (salida_dir / 'mediciones.csv').open('w', newline='') as archivo:
        writer = csv.DictWriter(archivo, fieldnames=campos)
        writer.writeheader()
        for caso, llave in casos.items():
            cifrado = salida_dir / f'{caso}.bin'
            ejecutar([ROOT / 'cifrar', texto, cifrado, llave], args.timeout)
            tareas = [(modo, n, 0) for modo, n in configs]
            medidas = [(modo, n, rep) for rep in range(1, args.repeticiones + 1) for modo, n in configs]
            generador.shuffle(medidas)
            for modo, n, rep in tareas + medidas:
                comando = ([ROOT / 'bruteforce_seq', cifrado, args.palabra, args.limite] if modo == 'secuencial'
                           else ['mpirun', '--host', f'localhost:{slots}', '--bind-to', 'none', '-np', n,
                                 ROOT / 'bruteforce_mpi', cifrado, args.palabra, args.limite, modo])
                salida, externo = ejecutar(comando, args.timeout)
                (salida_dir / f'{caso}-{modo}-{n}-{rep}.log').write_text(salida)
                encontrada = re.search(r'Llave encontrada\s*:\s*(\d+)', salida)
                if not encontrada or int(encontrada[1]) != llave:
                    raise RuntimeError(f'Llave incorrecta: {salida}')
                segundos = float(re.search(r'(?:Tiempo de busqueda|Tiempo paralelo)\s*:\s*([\d.eE+-]+)', salida)[1])
                intentos = int(re.search(r'Llaves probadas\s*:\s*(\d+)', salida)[1])
                print(f'{caso:10} {modo:10} np={n} rep={rep}: {segundos:.6f}s ({intentos} llaves)', flush=True)
                if rep:
                    fila = dict(zip(campos, [caso, llave, args.limite, modo, n, rep, segundos, externo, intentos, shlex.join(list(map(str, comando)))]))
                    filas.append(fila)
                    writer.writerow(fila)
                    archivo.flush()
    resumen = []
    for caso in casos:
        base = statistics.mean(f['segundos'] for f in filas if f['caso'] == caso and f['modo'] == 'secuencial')
        for modo, n in configs:
            tiempos = [f['segundos'] for f in filas if f['caso'] == caso and f['modo'] == modo and f['procesos'] == n]
            promedio = statistics.mean(tiempos)
            resumen.append({'caso': caso, 'modo': modo, 'procesos': n, 'repeticiones': len(tiempos),
                            'promedio_s': promedio, 'desviacion_s': statistics.stdev(tiempos) if len(tiempos) > 1 else 0,
                            'minimo_s': min(tiempos), 'maximo_s': max(tiempos),
                            'speedup': base / promedio, 'eficiencia': base / promedio / n})
    with (salida_dir / 'resumen.csv').open('w', newline='') as archivo:
        writer = csv.DictWriter(archivo, fieldnames=resumen[0].keys())
        writer.writeheader()
        writer.writerows(resumen)


if __name__ == '__main__':
    main()
