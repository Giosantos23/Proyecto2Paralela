/* ================================================================
 * des_utils.h
 * ----------------------------------------------------------------
 * Rutinas compartidas de cifrado/descifrado DES (OpenSSL) usadas
 * por la version secuencial, la paralela (MPI) y el utilitario de
 * cifrado. Centralizar estas funciones aqui evita duplicar codigo
 * y garantiza que las tres herramientas usan EXACTAMENTE el mismo
 * algoritmo.
 *
 * Proyecto #2 - Computacion Paralela y Distribuida (CC3069) - UVG
 * ================================================================ */
#ifndef DES_UTILS_H
#define DES_UTILS_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <openssl/des.h>

/* Tipo de la llave privada. DES usa 56 bits efectivos (los 8 bits
 * de paridad se descartan), por lo que uint64_t es suficiente. */
typedef uint64_t KeyType;

/* ----------------------------------------------------------------
 * Funcion: preparar_llave
 * Proposito: Convierte un valor numerico de llave en un bloque
 *            DES_cblock de 8 bytes con la paridad impar correcta
 *            que espera OpenSSL.
 * Entradas:  key_val (KeyType) - valor numerico de la llave.
 * Salidas:   key (DES_cblock&) - bloque de 8 bytes listo para DES.
 * ---------------------------------------------------------------- */
inline void preparar_llave(KeyType key_val, DES_cblock &key) {
    memcpy(key, &key_val, 8);
    DES_set_odd_parity(&key);
}

/* ----------------------------------------------------------------
 * Funcion: encrypt
 * Proposito: Cifra un texto en bloques de 8 bytes (modo ECB) con la
 *            llave indicada, sobreescribiendo el buffer de entrada.
 * Entradas:  key_val (KeyType) - llave de cifrado.
 *            texto (char*)     - buffer a cifrar (modificado in-situ).
 *            len (int)         - longitud del buffer (multiplo de 8).
 * Salidas:   texto queda cifrado en el mismo buffer.
 * ---------------------------------------------------------------- */
inline void encrypt(KeyType key_val, char *texto, int len) {
    DES_cblock key;
    DES_key_schedule schedule;
    preparar_llave(key_val, key);
    DES_set_key_unchecked(&key, &schedule);
    for (int i = 0; i < len; i += 8) {
        DES_ecb_encrypt((const_DES_cblock *)(texto + i),
                        (DES_cblock *)(texto + i), &schedule, DES_ENCRYPT);
    }
}

/* ----------------------------------------------------------------
 * Funcion: decrypt
 * Proposito: Descifra un texto en bloques de 8 bytes (modo ECB) con
 *            la llave indicada, sobreescribiendo el buffer.
 * Entradas:  key_val (KeyType) - llave candidata de descifrado.
 *            ciph (char*)      - buffer cifrado (modificado in-situ).
 *            len (int)         - longitud del buffer (multiplo de 8).
 * Salidas:   ciph queda descifrado en el mismo buffer.
 * ---------------------------------------------------------------- */
inline void decrypt(KeyType key_val, char *ciph, int len) {
    DES_cblock key;
    DES_key_schedule schedule;
    preparar_llave(key_val, key);
    DES_set_key_unchecked(&key, &schedule);
    for (int i = 0; i < len; i += 8) {
        DES_ecb_encrypt((const_DES_cblock *)(ciph + i),
                        (DES_cblock *)(ciph + i), &schedule, DES_DECRYPT);
    }
}

/* ----------------------------------------------------------------
 * Funcion: tryKey
 * Proposito: Prueba una llave candidata. Descifra una COPIA del
 *            texto cifrado (para no destruir el original) y verifica
 *            si la palabra clave aparece como substring. Es el
 *            predicado central del ataque de fuerza bruta.
 * Entradas:  key_val (KeyType)   - llave candidata.
 *            ciph (const char*)  - texto cifrado original (no se toca).
 *            len (int)           - longitud del texto cifrado.
 *            keyword (const char*) - frase clave que se busca.
 * Salidas:   Retorna true si la llave descifra el texto y este
 *            contiene la palabra clave; false en caso contrario.
 * ---------------------------------------------------------------- */
inline bool tryKey(KeyType key_val, const char *ciph, int len, const char *keyword) {
    /* Copia local para no alterar el buffer compartido. Se reserva
     * len+1 para poder colocar el terminador nulo que necesita strstr. */
    std::vector<char> temp(len + 1);
    memcpy(temp.data(), ciph, len);
    temp[len] = '\0';

    decrypt(key_val, temp.data(), len);

    return strstr(temp.data(), keyword) != nullptr;
}

#endif /* DES_UTILS_H */
