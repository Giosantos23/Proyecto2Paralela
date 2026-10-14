# Cifrados de otros grupos

Publicaciones consultadas en el foro de Canvas. Se conservan los archivos
originales de los grupos 1 y 7 y sus rangos publicados.

## Casos seleccionados

| Grupo | Archivo original local | Frase exacta | Rango publicado |
|---|---|---|---|
| 1 | equipo1_mensaje.bin | quetzal sincronizado en MPI | [0, 200000) |
| 7 | textoGrupo7.bin | se le olvida como manejar | [0, 4194304) |

Grupo 1, sección 40: Pablo Daniel Barillas Moreno y Milton Giovanni Polanco
Serrano. La publicación indica expresamente DES-ECB y que los archivos
`equipo1_mensaje.bin` y `equipo1_mensaje.des` tienen el mismo ciphertext.
Utilizamos el archivo `.bin`.

Grupo 7: Fabián Morales, Erick Guerra y Juan Cruz. Publicación de Erick Guerra
del 9 de octubre a las 21:39, editada el 14 de octubre a la 1:06. El adjunto
publicado se llama `texto.bin`; lo conservamos como `textoGrupo7.bin`. Se comprobó compatibilidad con DES-ECB al recuperar la
frase exacta, también mediante OpenSSL independiente.


Los hashes SHA-256 están en `resultados/otros-grupos-2026-10-14/fuentes.json`.
