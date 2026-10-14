#!/usr/bin/env python3
"""Mide cifrados originales, sin volver a cifrar ni cambiar sus bytes."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import random
import re
import shlex
import statistics
from datetime import datetime, timezone

from medir import ROOT, ejecutar


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archivo',type=Path,required=True)
    parser.add_argument('--grupo',type=int,required=True)
    parser.add_argument('--palabra',required=True)
    parser.add_argument('--llave',type=int,required=True)
    parser.add_argument('--limite',type=int,default=33554432)
    parser.add_argument('--repeticiones',type=int,default=10)
    parser.add_argument('--salida',type=Path,required=True)
    args=parser.parse_args()
    if not 0<=args.llave<args.limite<=2**56 or args.repeticiones<2:
        parser.error('Llave dentro del rango y al menos dos repeticiones.')
    archivo=args.archivo.resolve()
    original=archivo.read_bytes()
    if not original or len(original)%8:
        parser.error('El cifrado debe ser no vacío y múltiplo de ocho.')
    args.salida.mkdir(parents=True,exist_ok=False)
    carpeta=args.salida.resolve()
    def info(cmd): return ejecutar(cmd,20)[0].strip()
    slots=int(info(['/usr/sbin/sysctl','-n','hw.physicalcpu']))
    if slots<8: parser.error('Se requieren al menos ocho núcleos físicos.')
    metadata={'fecha_utc':datetime.now(timezone.utc).isoformat(),'grupo':args.grupo,
              'archivo_original':str(archivo),'bytes':len(original),
              'sha256_original':hashlib.sha256(original).hexdigest(),
              'palabra':args.palabra,'llave_efectiva':args.llave,'limite':args.limite,
              'repeticiones':args.repeticiones,'procesos':[2,4,6,8],'semilla':3069,
              'cpu':info(['/usr/sbin/sysctl','-n','machdep.cpu.brand_string']),
              'nucleos_fisicos':slots,'mpi':info(['mpirun','--version']),
              'compilador':info(['mpicxx','--version']),
              'openssl':info(['/opt/homebrew/opt/openssl@3/bin/openssl','version']),
              'commit':info(['git','-C',ROOT,'rev-parse','HEAD']),
              'binarios_sha256':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest()
                                  for n in ['bruteforce_seq','bruteforce_mpi']},
              'makefile':(ROOT/'Makefile').read_text(),
              'afinidad':'sin afinidad fija; localhost; sin sobresuscripción',
              'tiempo_speedup':'tiempo interno de búsqueda; cociente de promedios',
              'fuente':'Publicación de Canvas transcrita en textos_otros_grupos/README.md'}
    (carpeta/'entorno.json').write_text(json.dumps(metadata,indent=2,ensure_ascii=False)+'\n')
    for nombre,cmd in [('energia-inicio',['pmset','-g','batt']),('energia-configuracion',['pmset','-g','custom']),('carga-inicio',['uptime'])]:
        (carpeta/f'{nombre}.txt').write_text(info(cmd)+'\n')
    configs=[('secuencial',1)]+[(modo,n) for n in [2,4,6,8] for modo in ['bloque','ciclico']]
    tasks=[(modo,n,0) for modo,n in configs]
    medidas=[(modo,n,rep) for rep in range(1,args.repeticiones+1) for modo,n in configs]
    random.Random(3069).shuffle(medidas)
    fields=['caso','llave','limite','modo','procesos','repeticion','segundos','segundos_externos','intentos','comando']
    filas=[]
    plain=None
    with (carpeta/'mediciones.csv').open('w',newline='') as salida_csv:
        writer=csv.DictWriter(salida_csv,fieldnames=fields)
        writer.writeheader()
        for modo,n,rep in tasks+medidas:
            cmd=([ROOT/'bruteforce_seq',archivo,args.palabra,args.limite] if modo=='secuencial'
                 else ['mpirun','--host',f'localhost:{slots}','--bind-to','none','-np',n,
                       ROOT/'bruteforce_mpi',archivo,args.palabra,args.limite,modo])
            texto,externo=ejecutar(cmd,1200)
            (carpeta/f'{modo}-{n}-{rep}.log').write_text(texto)
            found=int(re.search(r'Llave encontrada\s*:\s*(\d+)',texto)[1])
            if found!=args.llave: raise RuntimeError('La llave encontrada difiere de la validada.')
            actual=texto.split('Texto descifrado : ',1)[1].rsplit('\nLlaves probadas',1)[0]
            if args.palabra not in actual: raise RuntimeError('La frase no está en el descifrado.')
            if plain is None:
                plain=actual
                (carpeta/'descifrado.txt').write_text(plain.rstrip('\0'))
            if plain!=actual: raise RuntimeError('Cambió el texto descifrado entre búsquedas.')
            secs=float(re.search(r'(?:Tiempo de busqueda|Tiempo paralelo)\s*:\s*([\d.eE+-]+)',texto)[1])
            intentos=int(re.search(r'Llaves probadas\s*:\s*(\d+)',texto)[1])
            if rep:
                row=dict(zip(fields,[f'grupo{args.grupo}',args.llave,args.limite,modo,n,rep,secs,externo,intentos,shlex.join(list(map(str,cmd)))]))
                filas.append(row)
                writer.writerow(row)
                salida_csv.flush()
            print(f'Grupo {args.grupo}: {modo} np={n} rep={rep}: {secs:.6f}s, {len(filas)}/{len(medidas)} medidas',flush=True)
    if archivo.read_bytes()!=original: raise RuntimeError('El archivo original cambió durante la corrida.')
    base=statistics.mean(r['segundos'] for r in filas if r['modo']=='secuencial')
    resumen=[]
    for modo,n in configs:
        ts=[r['segundos'] for r in filas if r['modo']==modo and r['procesos']==n]
        promedio=statistics.mean(ts)
        resumen.append({'caso':f'grupo{args.grupo}','modo':modo,'procesos':n,
                        'repeticiones':len(ts),'promedio_s':promedio,'desviacion_s':statistics.stdev(ts),
                        'minimo_s':min(ts),'maximo_s':max(ts),'speedup':base/promedio,'eficiencia':base/promedio/n})
    with (carpeta/'resumen.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=resumen[0]);writer.writeheader();writer.writerows(resumen)
    (carpeta/'validacion.json').write_text(json.dumps({'mediciones':len(filas),'configuraciones':len(configs),
         'repeticiones_por_configuracion':args.repeticiones,'archivo_original_sin_cambios':True,
         'llave_y_texto_iguales_en_todas_las_busquedas':True},indent=2)+'\n')
    for nombre,cmd in [('energia-final',['pmset','-g','batt']),('carga-final',['uptime'])]:
        (carpeta/f'{nombre}.txt').write_text(info(cmd)+'\n')


if __name__=='__main__': main()
