#!/usr/bin/env python3
"""Valida y resume muestras de CPU y actividad MPI por proceso."""
import argparse
from collections import defaultdict
import csv
import json
import math
from pathlib import Path
import re
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
plt.rcParams['text.parse_math'] = False

MPI = ['irecv','test','send','wait','waitany','waitall','bcast']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('carpeta',type=Path)
    args=parser.parse_args()
    carpeta=args.carpeta
    rows=list(csv.DictReader((carpeta/'perfiles.csv').open()))
    repeticiones=json.loads((carpeta/'entorno.json').read_text())['repeticiones']
    grupos=defaultdict(list)
    runs=defaultdict(list)
    for r in rows:
        key=r['caso'],r['modo'],int(r['procesos'])
        grupos[key].append(r)
        runs[*key,int(r['repeticion'])].append(r)
        total=float(r['total_s'])
        partes=float(r['busqueda_s'])+float(r['otro_s'])+sum(float(r[f'{m}_s']) for m in MPI)
        assert total>0 and math.isclose(total,partes,rel_tol=1e-7,abs_tol=1e-8)
        assert float(r['otro_s'])>=-1e-8
    assert len(grupos)==24 and len(runs)==24*repeticiones
    for (caso,modo,n,rep),rs in runs.items():
        assert sorted(int(r['rank']) for r in rs)==list(range(n))
        log=(carpeta/f'{caso}-{modo}-{n}-{rep}.log').read_text()
        assert sum(int(r['intentos']) for r in rs)==int(re.search(r'Llaves probadas\s*:\s*(\d+)',log)[1])
        assert int(re.search(r'Llave encontrada\s*:\s*(\d+)',log)[1])==int(rs[0]['llave'])
    resumen=[]
    for (caso,modo,n),rs in sorted(grupos.items()):
        total=sum(float(r['total_s']) for r in rs)
        local_runs=[runs[caso,modo,n,rep] for rep in range(1,repeticiones+1)]
        promedio=statistics.mean(max(float(r['total_s']) for r in run) for run in local_runs)
        intentos=statistics.mean(sum(int(r['intentos']) for r in run) for run in local_runs)
        resumen.append({'caso':caso,'modo':modo,'procesos':n,'repeticiones':repeticiones,
                        'promedio_max_rank_s':promedio,'intentos_totales_promedio':intentos,
                        'trabajo_vs_secuencial':intentos/(int(rs[0]['llave'])+1),
                        'busqueda_pct':100*sum(float(r['busqueda_s']) for r in rs)/total,
                        'mpi_pct':100*sum(float(r[f'{m}_s']) for r in rs for m in MPI)/total,
                        'otro_pct':100*sum(float(r['otro_s']) for r in rs)/total,
                        'test_llamadas_promedio':sum(int(r['test_llamadas']) for r in rs)/repeticiones})
    with (carpeta/'resumen_mpi.csv').open('w',newline='') as archivo:
        writer=csv.DictWriter(archivo,fieldnames=resumen[0])
        writer.writeheader()
        writer.writerows(resumen)
    # El resumen de sample omite funciones con menos de cinco muestras.
    sample=(carpeta/'cpu-sample.txt').read_text()
    total_muestras=int(re.search(r'^\s+(\d+) Thread_',sample,re.M)[1])
    collapsed=sample.split('Sort by top of stack, same collapsed (when >= 5):')[1].split('Binary Images:')[0]
    cpu=[]
    for line in collapsed.splitlines():
        match=re.match(r'\s+(.+?)\s+\(in .+?\)\s+(\d+)\s*$',line)
        if match:
            nombre=match[1]
            nombre='tryKey (código inline)' if nombre.startswith('tryKey(') else nombre
            cantidad=int(match[2])
            cpu.append({'funcion':nombre,'muestras':cantidad,'porcentaje':100*cantidad/total_muestras})
    omitidas=total_muestras-sum(r['muestras'] for r in cpu)
    assert omitidas>=0
    if omitidas:
        cpu.append({'funcion':'Otras (<5 muestras por función)','muestras':omitidas,'porcentaje':100*omitidas/total_muestras})
    with (carpeta/'cpu_resumen.csv').open('w',newline='') as archivo:
        writer=csv.DictWriter(archivo,fieldnames=cpu[0])
        writer.writeheader()
        writer.writerows(cpu)
    fig,ax=plt.subplots(figsize=(10,5),layout='constrained')
    ax.barh([r['funcion'] for r in cpu],[r['porcentaje'] for r in cpu],color='#2563eb')
    ax.invert_yaxis()
    ax.set_xlabel('Muestras con esa función en la cima de la pila (%)')
    ax.set_title(f'Perfil CPU · {total_muestras} muestras · ventana de 8 segundos')
    for i,r in enumerate(cpu):
        ax.text(r['porcentaje']+0.3,i,f"{r['porcentaje']:.2f}%",va='center',fontsize=9)
    ax.set_xlim(0,100)
    for ext in ['png','pdf']: fig.savefig(carpeta/f'cpu_funciones.{ext}',dpi=180)
    plt.close(fig)
    for metrica in ['tiempos','intentos']:
        fig,axes=plt.subplots(3,2,figsize=(11,10),layout='constrained')
        for i,caso in enumerate(['facil','intermedia','dificil']):
            for j,modo in enumerate(['bloque','ciclico']):
                ax=axes[i,j]
                rs=grupos[caso,modo,8]
                ranks=list(range(8))
                if metrica=='intentos':
                    valores=[statistics.mean(int(r['intentos']) for r in rs if int(r['rank'])==rank) for rank in ranks]
                    ax.bar(ranks,valores,color='#2563eb')
                    ax.set_ylabel('Intentos promedio por rank')
                else:
                    busqueda=[statistics.mean(float(r['busqueda_s']) for r in rs if int(r['rank'])==rank) for rank in ranks]
                    mpi=[statistics.mean(sum(float(r[f'{m}_s']) for m in MPI) for r in rs if int(r['rank'])==rank) for rank in ranks]
                    otro=[statistics.mean(float(r['otro_s']) for r in rs if int(r['rank'])==rank) for rank in ranks]
                    ax.bar(ranks,busqueda,label='Búsqueda',color='#2563eb')
                    ax.bar(ranks,mpi,bottom=busqueda,label='Dentro de MPI',color='#d97706')
                    ax.bar(ranks,otro,bottom=[a+b for a,b in zip(busqueda,mpi)],label='Control/instrumentación',color='#64748b')
                    ax.set_ylabel('Tiempo promedio local (s)')
                    if i==0 and j==0: ax.legend(fontsize=8)
                ax.set_title(f'{caso.capitalize()} · {modo} · 8 procesos')
                ax.set_xlabel('Rank')
                ax.set_xticks(ranks)
                ax.grid(axis='y',alpha=0.2)
        fig.suptitle(f'Perfil MPI por proceso · {repeticiones} repeticiones')
        for ext in ['png','pdf']: fig.savefig(carpeta/f'mpi_{metrica}_por_rank.{ext}',dpi=180)
        plt.close(fig)
    lines=['# Análisis de profiling','',
           'Esta sección técnica se incorporará después al informe final. El profiling se realizó el 14 de octubre de 2026, separado de las corridas de rendimiento.','',
           '## Método','',
           f'Se obtuvo una muestra CPU de ocho segundos con `sample` de macOS (intervalo solicitado 1 ms), y se ejecutaron {len(runs)} perfiles MPI ({repeticiones} repeticiones de 24 configuraciones), generando {len(rows)} registros por proceso. Los binarios instrumentados se compilaron con `-O2 -g`. El secuencial no agrega timers por llave. El MPI usa una capa PMPI y timers por lote.','',
           'CPU: texto propio, palabra `jacaranda`, rango `2^24`, llave 16777215. MPI: mismo texto, rango `2^22`, llaves fácil 2097153, intermedia 2621440 y difícil 921826; bloques y cíclico, procesos 2, 4, 6 y 8. Las ejecuciones MPI se realizaron una a la vez en orden aleatorio reproducible, sin sobresuscripción y sin fijar afinidad. No hubo calentamientos separados en esta corrida de profiling.','',
           'Se registraron energía y carga. El modo de bajo consumo estaba activado. No se controlaron otras aplicaciones. Las mediciones se limitan al equipo local y no representan un perfil de transferencia en una red entre máquinas.','',
           '## Funciones que concentran el costo','',
           f'La ventana de muestreo produjo **{total_muestras} muestras**. La tabla usa la función en la cima de la pila (muestras exclusivas), evitando sumar padres e hijos de una misma pila.','',
           '| Función | Muestras | Porcentaje |','|---|---:|---:|']
    for r in cpu: lines.append(f"| {r['funcion']} | {r['muestras']} | {r['porcentaje']:.2f}% |")
    des=next(r['porcentaje'] for r in cpu if r['funcion']=='DES_encrypt1')
    lines+=['',f'**El principal costo observado es DES:** `DES_encrypt1` aparece en {des:.2f}% de las muestras. Es la rutina interna utilizada al procesar bloques, incluso durante descifrado. La preparación del calendario de la llave (`DES_set_key_unchecked`) tiene un costo menor. Copias de memoria y paridad aparecen con proporciones pequeñas en este texto.', '',
            'Las rutinas inline de copia, preparación y búsqueda pueden quedar atribuidas a `tryKey`; no se asigna todo ese porcentaje únicamente al substring. Los porcentajes son estimaciones de muestreo de una ventana, no tiempos exactos ni un perfil del ciclo completo.','',
            '![Funciones CPU](cpu_funciones.png)','',
            '## Actividad MPI','',
            'Los porcentajes siguientes usan la suma de tiempos locales de todos los ranks y repeticiones (tiempo acumulado por proceso). No son porcentajes del tiempo global de pared. El tiempo máximo por ejecución se promedia por separado. La columna trabajo compara intentos totales con los intentos secuenciales esperados (llave + 1).','',
            '| Caso | Modo | Procesos | Máx. rank promedio (s) | Búsqueda | Dentro de MPI | Trabajo/seq |',
            '|---|---|---:|---:|---:|---:|---:|']
    for r in resumen:
        lines.append(f"| {r['caso']} | {r['modo']} | {r['procesos']} | {r['promedio_max_rank_s']:.6f} | {r['busqueda_pct']:.2f}% | {r['mpi_pct']:.2f}% | {r['trabajo_vs_secuencial']:.4f} |")
    ciclicos=[r for r in resumen if r['modo']=='ciclico']
    minimo=min(r['busqueda_pct'] for r in ciclicos)
    maxmpi=max(r['mpi_pct'] for r in ciclicos)
    lines+=['','## Hallazgos e interpretación','',
            f'1. **La búsqueda domina el reparto cíclico:** en las configuraciones medidas representa al menos {minimo:.2f}% del tiempo acumulado por proceso; las llamadas MPI representan como máximo {maxmpi:.2f}%. Esto indica que las esperas registradas dentro de MPI no dominan estas búsquedas locales. No mide ni descarta contención de CPU o memoria.',
            '2. **Los speedups aparentes de bloques se explican por menos trabajo.** La columna trabajo/seq permite ver las diferencias de candidatos evaluados. Para llaves al inicio de un bloque, el ganador hace muy pocos intentos y los otros procesos reciben STOP. Con ciertos repartos también se trabaja más que en el secuencial, porque otros ranks exploran simultáneamente zonas posteriores a la solución.',
            '3. **Se puede observar el reparto real por rank.** Los CSV y gráficas muestran que los intentos no son necesariamente iguales: intervienen la posición de la solución, el desempeño efectivo de cada proceso y cuándo se atiende STOP. La terminación es por lotes de hasta 1024 candidatos, por lo que un proceso puede completar su lote antes de detenerse.',
            '4. **Los perfiles muy cortos están afectados por el costo fijo de coordinación.** En casos de solución inmediata, unos microsegundos o milisegundos de espera pueden representar un porcentaje grande; no extrapolar esa proporción a búsquedas largas.',
            '5. **El perfil no demuestra una única causa para la eficiencia menor con ocho procesos.** No se identificaron los núcleos asignados ni se midieron contadores de memoria. La combinación de núcleos, planificación, competencia por recursos y trabajo extra debe considerarse como hipótesis, no como causa comprobada.',
            '6. **Prioridades de optimización futuras:** mantener el reparto cíclico para consistencia, revisar primero el costo de DES por candidato y, si se cambia el tamaño de lote, medir el equilibrio entre atención a MPI y trabajo extra antes de detenerse. No se modificó el algoritmo de búsqueda como parte de este profiling.','',
            '![Tiempo por rank](mpi_tiempos_por_rank.png)','','![Intentos por rank](mpi_intentos_por_rank.png)','',
            '## Alcance y evidencia','',
            '- `cpu-sample.txt`: árbol de llamadas y muestras originales.',
            '- `cpu_resumen.csv`: porcentajes derivados de muestras exclusivas.',
            '- `perfiles.csv`: todos los perfiles por rank, con tiempos y conteos por llamada MPI.',
            '- `resumen_mpi.csv`: resúmenes por caso, modo y procesos.',
            '- CSV individuales y logs: evidencia de cada ejecución y de las llaves encontradas.',
            '- `entorno.json`, registros de energía/carga y comandos CPU: reproducción.',
            '- Gráficas en PNG y PDF para incorporación posterior al informe.',
            '- `validacion.json`: integridad de ranks, resultados y partición de tiempos.','',
            'El tiempo dentro de MPI incluye envío, progreso y posibles esperas; no es tiempo puro de red. La instrumentación agrega costo y estos tiempos no reemplazan los speedups obtenidos sin instrumentación. Los perfiles excluyen arranque, lectura, difusión inicial, barrera inicial, impresión y recolección de perfiles. No se calculó una penalización exacta de instrumentación mediante corridas pareadas.','',
            'Reproducción: consultar `benchmarks/PROFILING.md`. El informe final está en `informe/Informe_Proyecto2_MPI.pdf`; la evidencia de los grupos 1 y 7 está en `resultados/otros-grupos-2026-10-14/`.']
    (carpeta/'ANALISIS_PROFILING.md').write_text('\n'.join(lines)+'\n')
    validacion={'ejecuciones_mpi':len(runs),'configuraciones_mpi':len(grupos),
                'filas_rank':len(rows),'repeticiones':repeticiones,'muestras_cpu':total_muestras,
                'suma_intentos_y_resultados':True,'particion_tiempos_consistente':True}
    (carpeta/'validacion.json').write_text(json.dumps(validacion,indent=2)+'\n')
    print(json.dumps(validacion,indent=2))


if __name__=='__main__':
    main()
