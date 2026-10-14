#!/usr/bin/env python3
"""Regresiones funcionales con timeouts: python3 tests/test_integration.py.
MPI_BINARY permite repetirlas con el binario de envios sin buffering.
MPIEXEC y MPIEXEC_FLAGS permiten adaptar el lanzador al entorno.
"""
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MPI_BINARY = Path(os.environ.get('MPI_BINARY', str(ROOT / 'bruteforce_mpi'))).resolve()
MPIEXEC = shlex.split(os.environ.get('MPIEXEC', 'mpirun'))
MPI_FLAGS = shlex.split(os.environ.get('MPIEXEC_FLAGS', '--host localhost --oversubscribe'))
RUNS = 0


class Integration(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='des-mpi-test-')
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.plain = self.dir / 'entrada.txt'
        self.plain.write_bytes(b'Prueba grupal con palabra jacaranda para MPI.')
        self.cipher = self.dir / 'texto.bin'

    def run_command(self, args, expected=0):
        global RUNS
        RUNS += 1
        args = [str(x) for x in args]
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            output = process.communicate(timeout=15)[0]
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                output = process.communicate(timeout=3)[0]
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                output = process.communicate()[0]
            self.fail(f'Bloqueo: {args}\n{output.decode(errors="replace")}')
        text = output.decode(errors='replace')
        self.assertEqual(process.returncode, expected, f'{args}\n{text}')
        return text

    def encrypt(self, key=42, legacy=False):
        return self.run_command([ROOT / 'cifrar', self.plain, self.cipher, key] +
                                (['--legacy'] if legacy else []))

    def search(self, limit, keyword='jacaranda', processes=None, mode='bloque', expected=0):
        command = ([ROOT / 'bruteforce_seq'] if processes is None else
                   MPIEXEC + MPI_FLAGS + ['-np', processes, MPI_BINARY])
        args = [self.cipher, keyword, limit] + ([] if processes is None else [mode])
        return self.run_command(command + args, expected)

    def assert_found(self, output, key, limit):
        found = re.findall(r'Llave encontrada\s*:\s*(\d+)', output)
        self.assertEqual(found, [str(key)])  # Solo rank 0 imprime el resultado.
        attempts = int(re.search(r'Llaves probadas\s*:\s*(\d+)', output)[1])
        self.assertGreater(attempts, 0)
        self.assertLessEqual(attempts, limit)
        self.assertIn(self.plain.read_text(), output)

    def test_roundtrip_boundaries_and_partitions(self):
        for key, limit in [(0, 1), (1, 2), (42, 129), (128, 129)]:
            self.encrypt(key)
            self.assert_found(self.search(limit), key, limit)
            for processes in [1, 2, 3, 4, 6]:
                for mode in ['bloque', 'ciclico']:
                    with self.subTest(key=key, processes=processes, mode=mode):
                        self.assert_found(self.search(limit, processes=processes, mode=mode), key, limit)

    def test_every_small_key_is_reachable(self):
        # Incluye comienzos y finales de segmentos y todos los residuos ciclicos.
        for key in range(8):
            self.encrypt(key)
            for mode in ['bloque', 'ciclico']:
                self.assert_found(self.search(8, processes=3, mode=mode), key, 8)

    def test_not_found_exhausts_exactly_once(self):
        self.encrypt(128)
        for processes in [None, 1, 3, 6]:
            for mode in ['bloque', 'ciclico']:
                for limit, keyword in [(128, 'jacaranda'), (129, 'no_existe_7829')]:
                    with self.subTest(processes=processes, mode=mode, limit=limit):
                        text = self.search(limit, keyword, processes, mode, expected=2)
                        self.assertIn('Llave NO encontrada', text)
                        self.assertRegex(text, rf'Llaves probadas\s*:\s*{limit}\b')

    def test_full_56_bit_limit_with_early_solution(self):
        self.encrypt(0)
        for processes in [None, 3]:
            for mode in ['bloque', 'ciclico']:
                self.assert_found(self.search(1 << 56, processes=processes, mode=mode), 0, 1 << 56)

    def test_multiple_valid_candidates(self):
        # Dos bloques independientes: tanto la llave 0 como la 1 producen
        # MPI_TEST en uno de los bloques. No depende de una colision aleatoria.
        self.plain.write_bytes(b'MPI_TEST')
        self.encrypt(0)
        first = self.cipher.read_bytes()
        self.encrypt(1)
        self.cipher.write_bytes(first + self.cipher.read_bytes())
        for repeat in range(4):
            for mode in ['bloque', 'ciclico']:
                text = self.search(2, 'MPI_TEST', processes=4, mode=mode)
                self.assertRegex(text, r'Llave encontrada\s*:\s*[01]\b')
                self.assertIn('MPI_TEST', text.split('Texto descifrado : ')[1])
                self.assertEqual(text.count('Llave encontrada'), 1)

    def test_legacy_fixture_from_original_commit(self):
        # Generado con cifrar.cpp y des_utils.h del commit b7ac31a,
        # llave numerica antigua 173800, texto de un bloque MPI_TEST.
        self.plain.write_bytes(b'MPI_TEST')
        self.encrypt(173800, legacy=True)
        self.assertEqual(self.cipher.read_bytes().hex(), '7aa95df74d0fb917')
        for processes in [None, 4]:
            for mode in ['bloque', 'ciclico']:
                self.assert_found(self.search(32768, 'MPI_TEST', processes, mode), 27124, 32768)

    def test_invalid_numbers(self):
        self.encrypt()
        bad = ['', '-1', '+1', 'abc', '12abc', ' 12', '12 ', '0x10',
               str((1 << 64)), str((1 << 56) + 1)]
        for number in bad + ['0']:
            for processes in [None, 2]:
                with self.subTest(number=number, processes=processes):
                    self.assertIn('Error:', self.search(number, processes=processes, expected=1))
        for number in bad + [str(1 << 56)]:
            with self.subTest(cipher_key=number):
                self.run_command([ROOT / 'cifrar', self.plain, self.dir / 'invalid.bin', number], 1)
                self.assertFalse((self.dir / 'invalid.bin').exists())

    def test_files_and_arguments(self):
        for data in [None, b'', b'bad']:
            if data is not None:
                self.cipher.write_bytes(data)
            for processes in [None, 2]:
                self.search(16, processes=processes, expected=1)
        self.encrypt()
        for processes in [None, 2]:
            self.search(128, '', processes, expected=1)
        self.search(128, processes=2, mode='incorrecto', expected=1)
        self.run_command([ROOT / 'bruteforce_seq'], 1)
        self.run_command([ROOT / 'bruteforce_seq', self.cipher, 'jacaranda', '128', 'extra'], 1)
        self.run_command(MPIEXEC + MPI_FLAGS + ['-np', 2, MPI_BINARY], 1)
        self.run_command(MPIEXEC + MPI_FLAGS + ['-np', 2, MPI_BINARY,
                         self.cipher, 'jacaranda', '128', 'bloque', 'extra'], 1)
        self.run_command([ROOT / 'cifrar', self.plain, self.cipher, 42, '--incorrecto'], 1)
        original = self.plain.read_bytes()
        self.run_command([ROOT / 'cifrar', self.plain, self.plain, 42], 1)
        self.assertEqual(self.plain.read_bytes(), original)
        self.run_command([ROOT / 'cifrar', self.plain, self.dir, 42], 1)
        self.run_command([ROOT / 'cifrar', self.dir / 'ausente.txt', self.cipher, 42], 1)

    def test_oversized_sparse_file(self):
        # No reserva 2 GiB de RAM/disco: verifica el rechazo antes de leer.
        with self.cipher.open('wb') as file:
            file.truncate(1 << 31)
        for processes in [None, 2]:
            self.search(16, processes=processes, expected=1)
        self.run_command([ROOT / 'cifrar', self.cipher, self.dir / 'big.bin', 1], 1)

    def test_text_limit_and_long_keyword(self):
        self.plain.write_text(' '.join(['jacaranda'] + ['computacion'] * 349))
        self.encrypt()
        self.assert_found(self.search(128), 42, 128)
        self.assert_found(self.search(128, processes=4, mode='ciclico'), 42, 128)
        for processes in [None, 2]:
            text = self.search(128, 'x' * (self.cipher.stat().st_size + 1), processes, expected=2)
            self.assertRegex(text, r'Llaves probadas\s*:\s*0\b')
        self.plain.write_text(self.plain.read_text() + ' extra')
        self.run_command([ROOT / 'cifrar', self.plain, self.dir / 'exceso.bin', 42], 1)
        self.assertFalse((self.dir / 'exceso.bin').exists())

    def test_plaintext_with_nul_and_unaligned_keyword(self):
        self.plain.write_bytes(b'x\0xxxxjacaranda!')
        self.encrypt(42)
        for processes in [None, 3]:
            text = self.search(128, processes=processes, mode='ciclico')
            self.assertIn('x\0xxxxjacaranda!', text)
            self.assertRegex(text, r'Llave encontrada\s*:\s*42\b')


if __name__ == '__main__':
    program = unittest.main(verbosity=2, exit=False)
    print(f'Ejecuciones verificadas: {RUNS}; binario MPI: {MPI_BINARY.name}', flush=True)
    raise SystemExit(0 if program.result.wasSuccessful() else 1)
