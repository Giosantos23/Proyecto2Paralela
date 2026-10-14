#!/usr/bin/env python3
"""Sampling de CPU en macOS y perfiles PMPI por rank, en corridas separadas."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shlex
import signal
import subprocess
import time
from datetime import datetime, timezone

from medir import ROOT, ejecutar


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--salida', type=Path, required=True)
    parser.add_argument('--repeticiones', type=int, default=3)
    args = parser.parse_args()
    if args.repeticiones < 1:
        parser.error('Repeticiones debe ser positivo')
    args.salida.mkdir(parents=True, exist_ok=False)
    carpeta = args.salida.resolve()
    fisicos = int(ejecutar(['/usr/sbin/sysctl', '-n', 'hw.physicalcpu'], 10)[0])
    if fisicos < 8:
        parser.error('Esta corrida requiere al menos ocho núcleos físicos.')
    def info(cmd):
        return ejecutar(cmd, 15)[0].strip()
    metadata = {
        'fecha_utc': datetime.now(timezone.utc).isoformat(),
        'cpu': info(['/usr/sbin/sysctl', '-n', 'machdep.cpu.brand_string']),
        'nucleos_fisicos': fisicos, 'mpi': info(['mpirun', '--version']),
        'compilador': info(['mpicxx', '--version']),
        'commit': info(['git', '-C', ROOT, 'rev-parse', 'HEAD']),
        'estado_git': info(['git', '-C', ROOT, 'status', '--short']),
        'repeticiones': args.repeticiones, 'limite_mpi': 4194304,
        'semilla_orden': 3069, 'procesos': [2,4,6,8],
        'sampling': 'sample, 8 segundos, intervalo 1 ms; ejecutable -O2 -g',
        'instrumentacion': 'PMPI y medición de lotes; ventana de búsqueda hasta Bcast final; Gather de perfiles fuera de ventana',
        'sha256': {nombre: hashlib.sha256((ROOT / '.profile-build' / nombre).read_bytes()).hexdigest()
                   for nombre in ['bruteforce_seq','bruteforce_mpi']},
    }
    (carpeta / 'entorno.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False))
    for nombre, cmd in [('energia-inicio', ['pmset','-g','batt']), ('energia-configuracion',['pmset','-g','custom']), ('carga-inicio',['uptime'])]:
        (carpeta / f'{nombre}.txt').write_text(info(cmd)+'\n')
    # Muestreo sobre una búsqueda larga; no se agregan timers por candidato.
    limite_cpu = 1 << 24
    cifrado = carpeta / 'cpu.bin'
    ejecutar([ROOT/'cifrar', ROOT/'prueba.txt', cifrado, limite_cpu-1], 300)
    cmd = [ROOT/'.profile-build/bruteforce_seq', cifrado, 'jacaranda', limite_cpu]
    with (carpeta / 'cpu-busqueda.log').open('w') as log:
        proc = subprocess.Popen(list(map(str,cmd)), stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            time.sleep(1)
            sample_cmd = ['/usr/bin/sample', proc.pid, 8, 1, '-mayDie', '-file', carpeta/'cpu-sample.txt']
            salida, _ = ejecutar(sample_cmd, 60)
            (carpeta / 'cpu-sample-ejecucion.log').write_text(salida)
            proc.wait(timeout=300)
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        if proc.returncode != 0:
            raise RuntimeError('Falló búsqueda de CPU; ver cpu-busqueda.log')
    texto_cpu = (carpeta/'cpu-busqueda.log').read_text()
    if int(re.search(r'Llave encontrada\s*:\s*(\d+)',texto_cpu)[1]) != limite_cpu-1:
        raise RuntimeError('Llave incorrecta en sampling')
    (carpeta/'cpu-comandos.txt').write_text(shlex.join(list(map(str,cmd)))+'\n'+shlex.join(list(map(str,sample_cmd)))+'\n')
    print('Sampling CPU completado.', flush=True)
    limite = 4194304
    casos = {'facil': limite//2+1, 'intermedia':limite//2+limite//8,
             'dificil':(limite+6)//7+(limite+12)//13}
    cifrados = {}
    for caso,llave in casos.items():
        cifrados[caso] = carpeta/f'{caso}.bin'
        ejecutar([ROOT/'cifrar',ROOT/'prueba.txt',cifrados[caso],llave],300)
    tareas=[(caso,modo,n,rep) for caso in casos for modo in ['bloque','ciclico']
            for n in [2,4,6,8] for rep in range(1,args.repeticiones+1)]
    random.Random(3069).shuffle(tareas)
    campos=None
    with (carpeta/'perfiles.csv').open('w',newline='') as archivo:
        writer=None
        for caso,modo,n,rep in tareas:
            prefijo=f'{caso}-{modo}-{n}-{rep}'
            perfil=carpeta/f'{prefijo}.csv'
            cmd=['mpirun','--host',f'localhost:{fisicos}','--bind-to','none','-np',n,
                 ROOT/'.profile-build/bruteforce_mpi',cifrados[caso],'jacaranda',limite,modo]
            env=os.environ.copy()
            env['PROFILE_CSV']=str(perfil)
            proceso=subprocess.Popen(list(map(str,cmd)),env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
            try:
                salida=proceso.communicate(timeout=300)[0].decode(errors='replace')
            except subprocess.TimeoutExpired:
                os.killpg(proceso.pid,signal.SIGKILL)
                proceso.communicate()
                raise RuntimeError(f'Timeout en {prefijo}')
            (carpeta/f'{prefijo}.log').write_text(salida)
            if proceso.returncode or int(re.search(r'Llave encontrada\s*:\s*(\d+)',salida)[1])!=casos[caso]:
                raise RuntimeError(f'Falló {prefijo}: {salida}')
            with perfil.open() as entrada:
                filas=list(csv.DictReader(entrada))
            assert sorted(int(f['rank']) for f in filas)==list(range(n))
            assert sum(int(f['intentos']) for f in filas)==int(re.search(r'Llaves probadas\s*:\s*(\d+)',salida)[1])
            for f in filas:
                fila={'caso':caso,'llave':casos[caso],'repeticion':rep,**f}
                if writer is None:
                    campos=list(fila)
                    writer=csv.DictWriter(archivo,fieldnames=campos)
                    writer.writeheader()
                writer.writerow(fila)
            archivo.flush()
            print(f'{prefijo}: {n} ranks verificados',flush=True)
    for nombre,cmd in [('energia-final',['pmset','-g','batt']),('carga-final',['uptime'])]:
        (carpeta/f'{nombre}.txt').write_text(info(cmd)+'\n')


if __name__=='__main__':
    main()
