/* ================================================================
 * des_utils.h - DES-ECB compartido por cifrador y buscadores.
 * CC3069 - Proyecto #2, UVG.
 * ================================================================ */
#ifndef DES_UTILS_H
#define DES_UTILS_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <openssl/des.h>

using KeyType = std::uint64_t;
constexpr KeyType ESPACIO_DES = KeyType{1} << 56;
constexpr KeyType SIN_LLAVE = ESPACIO_DES; // Fuera de las llaves validas.
constexpr KeyType LIMITE_POR_DEFECTO = KeyType{1} << 25;

/* Entrada: indice de 56 bits en [0, ESPACIO_DES).
 * Salida: 8 bytes DES, con 7 bits utiles por byte y paridad impar.
 * Los grupos menos significativos se colocan primero, sin depender
 * del endianness del equipo. Cada indice representa una llave unica. */
inline void preparar_llave(KeyType indice, DES_cblock &key) {
    for (int i = 0; i < 8; ++i) {
        key[i] = static_cast<unsigned char>(((indice >> (7 * i)) & 0x7f) << 1);
    }
    DES_set_odd_parity(&key);
}

/* Entrada: entero que el programa anterior copiaba en memoria little-endian.
 * Salida: indice efectivo equivalente; elimina los bits de paridad.
 * Permite reproducir los cifrados anteriores con cifrar --legacy. */
inline KeyType llave_legacy_a_efectiva(KeyType antigua) {
    KeyType efectiva = 0;
    for (int i = 0; i < 8; ++i) {
        efectiva |= ((antigua >> (8 * i + 1)) & 0x7f) << (7 * i);
    }
    return efectiva;
}

/* Entrada: indice efectivo. Salida: los 8 bytes DES como 16 digitos hex.
 * Esta representacion permite comparar llaves entre herramientas. */
inline std::string llave_hex(KeyType indice) {
    DES_cblock key;
    preparar_llave(indice, key);
    std::ostringstream salida;
    salida << std::hex << std::setfill('0');
    for (unsigned char byte : key) salida << std::setw(2) << unsigned(byte);
    return salida.str();
}

/* Entradas: indice efectivo, buffer y longitud multiplo de 8.
 * Salida: buffer transformado in situ en modo DES-ECB. Los bloques
 * locales evitan depender de la alineacion o aliasing del vector. */
inline void transformar_des(KeyType indice, char *texto, std::size_t len, int modo) {
    DES_cblock key;
    DES_key_schedule schedule;
    preparar_llave(indice, key);
    DES_set_key_unchecked(&key, &schedule);
    for (std::size_t i = 0; i < len; i += 8) {
        DES_cblock entrada, salida;
        std::memcpy(entrada, texto + i, 8);
        DES_ecb_encrypt(&entrada, &salida, &schedule, modo);
        std::memcpy(texto + i, salida, 8);
    }
}

inline void encrypt(KeyType key, char *texto, std::size_t len) {
    transformar_des(key, texto, len, DES_ENCRYPT);
}

inline void decrypt(KeyType key, char *texto, std::size_t len) {
    transformar_des(key, texto, len, DES_DECRYPT);
}

/* Entradas: llave candidata, cifrado inmutable, palabra no vacia y
 * buffer de trabajo reutilizable. Salida: true si aparece la palabra.
 * Se busca por longitud, incluso si el descifrado contiene bytes nulos.
 * No se reserva memoria nueva en cada intento. */
inline bool tryKey(KeyType key, const std::vector<char> &ciph,
                   const std::string &keyword, std::vector<char> &trabajo) {
    trabajo.resize(ciph.size());
    std::copy(ciph.begin(), ciph.end(), trabajo.begin());
    decrypt(key, trabajo.data(), trabajo.size());
    return std::search(trabajo.begin(), trabajo.end(), keyword.begin(), keyword.end())
           != trabajo.end();
}

#endif
