#include <mpi.h>
#include <iostream>
#include <cstring>
#include <openssl/des.h> 
typedef uint64_t KeyType; 

void preparar_llave(KeyType key_val, DES_cblock &key) {
    memcpy(key, &key_val, 8);
    DES_set_odd_parity(&key);
}

void encrypt(KeyType key_val, char *texto, int len) {
    DES_cblock key;
    DES_key_schedule schedule;
    
    preparar_llave(key_val, key);
    DES_set_key_unchecked(&key, &schedule);
    
    for (int i = 0; i < len; i += 8) {
        DES_ecb_encrypt((const_DES_cblock*)(texto + i), (DES_cblock*)(texto + i), &schedule, DES_ENCRYPT);
    }
}

void decrypt(KeyType key_val, char *ciph, int len) {
    DES_cblock key;
    DES_key_schedule schedule;
    
    preparar_llave(key_val, key);
    DES_set_key_unchecked(&key, &schedule);
    for (int i = 0; i < len; i += 8) {
        DES_ecb_encrypt((const_DES_cblock*)(ciph + i), (DES_cblock*)(ciph + i), &schedule, DES_DECRYPT);
    }
}
bool tryKey(KeyType key_val, char *ciph, int len, const char* keyword) {
    char temp[1024]; 
    memcpy(temp, ciph, len);
    temp[len] = '\0';
    
    decrypt(key_val, temp, len);
    
    if (strstr(temp, keyword) != nullptr) {
        std::cout << "Llave encontrada: " << key_val << "\n";
        std::cout << "Texto descifrado: " << temp << "\n";
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char mensaje[1024] = "Hola como estas, este es un mensaje secreto que vamos a cifrar y luego descifrar usando fuerza bruta.";
    const char* keyword = "paralela";
    
    KeyType llave_secreta = 123456; 
    
    int len = strlen(mensaje);
    int padding = 8 - (len % 8);
    if (padding != 8) {
        for (int i = 0; i < padding; i++) mensaje[len + i] = ' '; // Relleno con espacios
        len += padding;
        mensaje[len] = '\0';
    }

    if (rank == 0) {
        std::cout << "Texto original: " << mensaje << "\n";
        
        encrypt(llave_secreta, mensaje, len);
        std::cout << "Texto cifrado: " << mensaje << "\n\n";
        
        std::cout << "Iniciando fuerza bruta \n";
        double start_time = MPI_Wtime();
        
        for (KeyType i = 0; i < 500000; i++) {
            if (tryKey(i, mensaje, len, keyword)) {
                break; 
            }
        }
        
        double end_time = MPI_Wtime();
        std::cout << "Tiempo de busqueda: " << (end_time - start_time) << " segundos.\n";
    }

    MPI_Finalize();
    return 0;
}