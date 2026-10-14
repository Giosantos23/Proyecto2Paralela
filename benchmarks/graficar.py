#!/usr/bin/env python3
"""Genera gráficas PNG y PDF a partir del resumen de una corrida completa."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('carpeta', type=Path)
    args = parser.parse_args()
    with (args.carpeta / 'resumen.csv').open() as archivo:
        filas = list(csv.DictReader(archivo))
    casos = list(dict.fromkeys(f['caso'] for f in filas))
    categoria = 'texto externo' if all(c.startswith('grupo') for c in casos) else 'posición de llave'
    procesos = sorted({int(f['procesos']) for f in filas if f['modo'] != 'secuencial'})
    for metrica, titulo, ylabel in [
        ('speedup', f'Speedup por {categoria}', 'T secuencial / T paralelo'),
        ('eficiencia', f'Eficiencia por {categoria}', 'Speedup / procesos (%)'),
        ('promedio_s', f'Tiempo de búsqueda por {categoria}', 'Promedio ± desviación estándar (s)'),
    ]:
        fig, axes_grid = plt.subplots(1, len(casos), figsize=(max(6, 4.33 * len(casos)), 4),
                                     layout='constrained', squeeze=False)
        axes = axes_grid[0]
        for ax, caso in zip(axes, casos):
            subset = [f for f in filas if f['caso'] == caso]
            for modo, color in [('bloque', '#d97706'), ('ciclico', '#2563eb')]:
                datos = sorted([f for f in subset if f['modo'] == modo], key=lambda f: int(f['procesos']))
                factor = 100 if metrica == 'eficiencia' else 1
                y = [float(f[metrica]) * factor for f in datos]
                if metrica == 'promedio_s':
                    ax.errorbar(procesos, y, yerr=[float(f['desviacion_s']) for f in datos],
                                color=color, marker='o', capsize=3, label=modo)
                else:
                    ax.plot(procesos, y, color=color, marker='o', label=modo)
            if metrica == 'speedup':
                ax.plot(procesos, procesos, '--', color='gray', label='Referencia S = N')
                ax.set_yscale('log')
            elif metrica == 'eficiencia':
                ax.axhline(100, linestyle='--', color='gray', label='Referencia 100%')
                ax.set_yscale('log')
            else:
                sec = float(next(f['promedio_s'] for f in subset if f['modo'] == 'secuencial'))
                ax.axhline(sec, linestyle='--', color='gray', label='Secuencial')
            ax.set_title(caso.capitalize())
            ax.set_xlabel('Procesos MPI')
            ax.set_ylabel(ylabel)
            ax.set_xticks(procesos)
            ax.grid(alpha=0.25)
            ax.legend(fontsize=8)
        repeticiones = filas[0]['repeticiones']
        fig.suptitle(f'{titulo} · {repeticiones} repeticiones por configuración')
        for extension in ['png', 'pdf']:
            fig.savefig(args.carpeta / f'{metrica}.{extension}', dpi=180)
        plt.close(fig)


if __name__ == '__main__':
    main()
