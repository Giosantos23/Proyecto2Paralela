# ================================================================
# Makefile - Proyecto #2 Computacion Paralela (CC3069) - UVG
# ----------------------------------------------------------------
# Compila las tres herramientas:
#   bruteforce_seq : version secuencial (baseline de speedup)
#   bruteforce_mpi : version paralela con OpenMPI
#   cifrar         : utilitario para generar textos cifrados .bin
#
# Requiere: OpenMPI (mpicxx) y OpenSSL (libcrypto).
# En macOS/Homebrew se detecta openssl@3 automaticamente.
# ================================================================

# Prefijo de OpenSSL (Homebrew en macOS; /usr en Linux tipico).
OPENSSL ?= $(shell brew --prefix openssl@3 2>/dev/null || echo /usr)

CXX       = mpicxx
CXXFLAGS  = -O2 -std=c++17 -Wall -Wno-deprecated-declarations -I$(OPENSSL)/include
LDFLAGS   = -L$(OPENSSL)/lib -lcrypto

BINARIOS  = bruteforce_seq bruteforce_mpi cifrar

all: $(BINARIOS)

bruteforce_seq: bruteforce_seq.cpp des_utils.h
	$(CXX) $(CXXFLAGS) -o $@ bruteforce_seq.cpp $(LDFLAGS)

bruteforce_mpi: bruteforce_mpi.cpp des_utils.h
	$(CXX) $(CXXFLAGS) -o $@ bruteforce_mpi.cpp $(LDFLAGS)

cifrar: cifrar.cpp des_utils.h
	$(CXX) $(CXXFLAGS) -o $@ cifrar.cpp $(LDFLAGS)

clean:
	rm -f $(BINARIOS)

.PHONY: all clean
