#pragma once

// Sólo existe en el ejecutable instrumentado; el binario normal no lo usa.
void perfil_iniciar();
void perfil_lote(double segundos);
void perfil_finalizar(int rank, int size, unsigned long long intentos,
                      double total, bool ciclico);
