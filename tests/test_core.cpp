/* Pruebas de DES y del espacio de busqueda, sin lanzar procesos MPI. */
#include <array>
#include <set>
#include "app_utils.h"

void exigir(bool condicion, const char *mensaje) {
    if (!condicion) throw std::runtime_error(mensaje);
}

int main() {
    try {
        // Vector DES conocido: clave 133457799BBCDFF1, texto 0123456789ABCDEF.
        const KeyType key = llave_legacy_a_efectiva(0xf1dfbc9b79573413ULL);
        exigir(llave_hex(key) == "133457799bbcdff1", "representacion DES incorrecta");
        std::array<unsigned char, 8> plain{0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
        const std::array<unsigned char, 8> expected{0x85,0xe8,0x13,0x54,0x0f,0x0a,0xb4,0x05};
        auto block = plain;
        encrypt(key, reinterpret_cast<char *>(block.data()), block.size());
        exigir(block == expected, "fallo el vector conocido de cifrado DES");
        decrypt(key, reinterpret_cast<char *>(block.data()), block.size());
        exigir(block == plain, "fallo el vector conocido de descifrado DES");

        exigir(llave_hex(0) == "0101010101010101", "llave cero incorrecta");
        exigir(llave_hex(ESPACIO_DES - 1) == "fefefefefefefefe", "llave maxima incorrecta");
        exigir(llave_legacy_a_efectiva(42) == 21, "conversion legacy incorrecta");
        exigir(llave_legacy_a_efectiva(43) == 21, "paridad legacy incorrecta");
        exigir(llave_hex(42) != llave_hex(43), "indices adyacentes duplicados");
        std::set<std::array<unsigned char, 8>> keys;
        for (KeyType k = 0; k < 65536; ++k) {
            DES_cblock bytes;
            preparar_llave(k, bytes);
            exigir(DES_check_key_parity(&bytes) == 1, "paridad invalida");
            std::array<unsigned char, 8> copy;
            std::copy(std::begin(bytes), std::end(bytes), copy.begin());
            keys.insert(copy);
        }
        exigir(keys.size() == 65536, "hay llaves efectivas duplicadas");
        for (int bit = 0; bit < 56; ++bit) {
            DES_cblock bytes;
            const KeyType indice = KeyType{1} << bit;
            preparar_llave(indice, bytes);
            KeyType raw = 0;
            for (int i = 0; i < 8; ++i) raw |= KeyType{bytes[i]} << (8 * i);
            exigir(llave_legacy_a_efectiva(raw) == indice, "se perdio un bit efectivo");
        }

        // La palabra puede cruzar bloques y aparecer despues de un byte nulo.
        std::vector<char> mensaje{'x','\0','x','x','x','x','j','a','c','a','r','a','n','d','a','!'};
        const auto original = mensaje;
        encrypt(ESPACIO_DES - 1, mensaje.data(), mensaje.size());
        const auto copia_cifrada = mensaje;
        std::vector<char> buffer;
        exigir(tryKey(ESPACIO_DES - 1, mensaje, "jacaranda", buffer), "busqueda binaria truncada");
        exigir(buffer == original && mensaje == copia_cifrada, "se modifico el cifrado original");

        for (KeyType limite : {1,2,3,5,7,64,127}) {
            for (int procesos : {1,2,3,4,6,8}) {
                for (bool ciclico : {false, true}) {
                    std::vector<int> vistos(limite, 0);
                    for (int r = 0; r < procesos; ++r) {
                        auto rango = repartir(limite, r, procesos, ciclico);
                        for (KeyType k = rango.siguiente; k < rango.fin; k += rango.paso) ++vistos[k];
                    }
                    exigir(std::all_of(vistos.begin(), vistos.end(), [](int n) { return n == 1; }),
                           "reparto con huecos o solapamientos");
                }
            }
        }
        for (int procesos : {1,3,6,127}) {
            KeyType fin = 0, total_ciclico = 0;
            for (int r = 0; r < procesos; ++r) {
                const auto bloque = repartir(ESPACIO_DES, r, procesos, false);
                exigir(bloque.siguiente == fin, "hueco en espacio completo");
                fin = bloque.fin;
                const auto ciclo = repartir(ESPACIO_DES, r, procesos, true);
                total_ciclico += 1 + (ciclo.fin - 1 - ciclo.siguiente) / ciclo.paso;
            }
            exigir(fin == ESPACIO_DES && total_ciclico == ESPACIO_DES, "cobertura de 56 bits incompleta");
        }
        std::cout << "OK: vector DES, 65536 llaves unicas, 56 bits, paridad, legacy, substring y reparto.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FALLO: " << error.what() << '\n';
        return 1;
    }
}
