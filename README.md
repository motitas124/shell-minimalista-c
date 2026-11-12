# rsh — Shell Minimalista en C

Un intérprete de comandos tipo Unix escrito desde cero en C, que reproduce el núcleo de cómo funcionan `bash` o `zsh`: crear procesos, conectarlos con tuberías y redirigir su entrada y salida.

![C](https://img.shields.io/badge/C-11-00599C?logo=c&logoColor=white)
![POSIX](https://img.shields.io/badge/POSIX-Linux%20%7C%20macOS%20%7C%20WSL-lightgrey)
![Tests](https://img.shields.io/badge/tests-11%20passing-brightgreen)

## Funcionalidades

| Función | Ejemplo |
|---|---|
| Ejecutar programas con argumentos | `ls -la /tmp` |
| Tuberías de N comandos | `cat log.txt \| grep ERROR \| wc -l` |
| Redirección de entrada | `sort < nombres.txt` |
| Redirección de salida (sobrescribir / anexar) | `ls > lista.txt`, `echo fin >> log.txt` |
| Comillas simples y dobles | `echo "hola   mundo"` |
| Comandos internos | `cd`, `pwd`, `history`, `help`, `exit` |
| Ctrl+C no cierra el shell, solo el proceso en ejecución | |

```
rsh:proyectos$ printf 'pera\nmanzana\nuva\n' | sort | head -n 2
manzana
pera
rsh:proyectos$ echo "hola desde rsh" > saludo.txt
rsh:proyectos$ cat < saludo.txt
hola desde rsh
```

## Cómo funciona

1. **Lectura**: `getline` lee la línea completa y se guarda en el historial.
2. **Tokenización**: separa palabras respetando comillas y reconoce los operadores `|`, `<`, `>`, `>>` aunque estén pegados (`ls|wc`).
3. **Análisis**: agrupa los tokens en comandos y asigna a cada uno sus archivos de entrada/salida.
4. **Ejecución**:
   - Si es un comando interno (`cd`, `exit`…), se ejecuta en el propio proceso del shell, porque cambiar de directorio en un hijo no afectaría al padre.
   - Si no, por cada comando se hace `fork()`. El hijo conecta las tuberías con `pipe()` + `dup2()`, aplica las redirecciones y reemplaza su imagen con `execvp()`.
   - El padre cierra sus copias de los descriptores y espera a todos los hijos con `waitpid()`.

```
 rsh ──fork──► hijo 1 (cat)  ──pipe──► hijo 2 (grep) ──pipe──► hijo 3 (wc)
  │                                                                  │
  └──────────────────────── waitpid() ◄──────────────────────────────┘
```

## Cómo ejecutarlo

Requiere un sistema POSIX (Linux, macOS o WSL en Windows), `gcc` y `make`.

```bash
make            # compila
./rsh           # inicia el shell (Ctrl+D o 'exit' para salir)
make test       # corre las pruebas de integración
```

## Lo que practiqué

- Llamadas al sistema de procesos: `fork`, `execvp`, `waitpid`.
- Comunicación entre procesos con `pipe` y redirección con `dup2`.
- Manejo de descriptores de archivo y por qué hay que cerrarlos (si no, la tubería nunca recibe EOF).
- Manejo de señales (`SIGINT`) en el padre y en los hijos.
- Escribir un tokenizador a mano con soporte de comillas.
- Pruebas de integración automatizadas con un script de shell.

## Posibles mejoras

- Ejecución en segundo plano con `&` y control de trabajos (`jobs`, `fg`).
- Expansión de variables de entorno (`$HOME`) y comodines (`*.c`).

## Autor

**Rendy Silva** — Estudiante de Ingeniería en Software, Universidad de las Américas (UDLA), Quito.
