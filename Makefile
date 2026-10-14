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
CXXFLAGS  = -O2 -std=c++17 -Wall -Wextra -Wpedantic -Wno-deprecated-declarations -I$(OPENSSL)/include
LDFLAGS   = -L$(OPENSSL)/lib -lcrypto
PYTHON    ?= python3

BINARIOS  = bruteforce_seq bruteforce_mpi cifrar
HEADERS   = des_utils.h app_utils.h

all: $(BINARIOS)

bruteforce_seq: bruteforce_seq.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ bruteforce_seq.cpp $(LDFLAGS)

bruteforce_mpi: bruteforce_mpi.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ bruteforce_mpi.cpp $(LDFLAGS)

cifrar: cifrar.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ cifrar.cpp $(LDFLAGS)

.test-build:
	mkdir -p $@

.test-build/test_core: tests/test_core.cpp $(HEADERS) | .test-build
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/test_core.cpp $(LDFLAGS)

.test-build/bruteforce_mpi_sync: bruteforce_mpi.cpp tests/synchronous_send.cpp $(HEADERS) | .test-build
	$(CXX) $(CXXFLAGS) -o $@ bruteforce_mpi.cpp tests/synchronous_send.cpp $(LDFLAGS)

test: all .test-build/test_core
	./.test-build/test_core
	$(PYTHON) tests/test_integration.py

test-sync: all .test-build/bruteforce_mpi_sync
	MPI_BINARY="$(CURDIR)/.test-build/bruteforce_mpi_sync" $(PYTHON) tests/test_integration.py

clean:
	rm -f $(BINARIOS)
	rm -rf .test-build .profile-build

.PHONY: all clean test test-sync

# Binarios independientes con símbolos e instrumentación para profiling.
.profile-build:
	mkdir -p $@

.profile-build/bruteforce_seq: bruteforce_seq.cpp $(HEADERS) | .profile-build
	$(CXX) $(CXXFLAGS) -g -o $@ bruteforce_seq.cpp $(LDFLAGS)

.profile-build/bruteforce_mpi: bruteforce_mpi.cpp benchmarks/profile_mpi.cpp benchmarks/profile_mpi.h $(HEADERS) | .profile-build
	$(CXX) $(CXXFLAGS) -g -DPERFIL_MPI -o $@ bruteforce_mpi.cpp benchmarks/profile_mpi.cpp $(LDFLAGS)

profile: .profile-build/bruteforce_seq .profile-build/bruteforce_mpi

.PHONY: profile
