/*
 * rsh - un shell minimalista en C para sistemas POSIX (Linux / macOS / WSL).
 *
 * Soporta:
 *   - Ejecucion de programas con argumentos (fork + execvp + waitpid)
 *   - Tuberias entre comandos:            ls -l | grep .c | wc -l
 *   - Redireccion de entrada y salida:    sort < datos.txt > ordenado.txt
 *   - Redireccion en modo append:         echo hola >> log.txt
 *   - Comillas simples y dobles:        echo "hola   mundo"
 *   - Comandos internos: cd, pwd, history, help, exit
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS      64
#define MAX_COMANDOS  16
#define MAX_HISTORIAL 50

typedef struct {
    char *argv[MAX_ARGS];
    char *entrada;   /* archivo para '<'  */
    char *salida;    /* archivo para '>' o '>>' */
    int   anexar;    /* 1 si es '>>' */
} Comando;

static char *historial[MAX_HISTORIAL];
static int   total_historial = 0;

static void agregar_historial(const char *linea) {
    if (total_historial == MAX_HISTORIAL) {
        free(historial[0]);
        memmove(historial, historial + 1, (MAX_HISTORIAL - 1) * sizeof *historial);
        total_historial--;
    }
    historial[total_historial++] = strdup(linea);
}

/*
 * Tokenizador: separa la linea en tokens respetando comillas simples y
 * dobles ("hola mundo" es un solo argumento). Los operadores |, <, > y >>
 * se reconocen aunque esten pegados a otras palabras (ls|wc). Los tokens se
 * copian a un buffer propio, por lo que la linea original no se modifica.
 */
#define MAX_TOKENS 256

typedef struct {
    char *texto;
    int   es_operador;
} Token;

static char  buffer_tokens[8192];
static Token tokens[MAX_TOKENS];

static int tokenizar(const char *linea) {
    char *escritura = buffer_tokens;
    char *limite = buffer_tokens + sizeof buffer_tokens - 4;
    const char *p = linea;
    int n = 0;

    while (*p && n < MAX_TOKENS) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        tokens[n].texto = escritura;
        if (*p == '|' || *p == '<' || *p == '>') {
            tokens[n].es_operador = 1;
            *escritura++ = *p++;
            if (p[-1] == '>' && *p == '>') *escritura++ = *p++;
        } else {
            tokens[n].es_operador = 0;
            char comilla = 0;
            while (*p && escritura < limite) {
                if (comilla) {
                    if (*p == comilla) { comilla = 0; p++; continue; }
                } else if (*p == '\'' || *p == '"') {
                    comilla = *p++;
                    continue;
                } else if (strchr(" \t|<>", *p)) {
                    break;
                }
                *escritura++ = *p++;
            }
            if (comilla) return -1; /* comilla sin cerrar */
        }
        *escritura++ = '\0';
        n++;
    }
    return n;
}

/* Agrupa los tokens en comandos separados por '|' y reconoce redirecciones. */
static int analizar(const char *linea, Comando *cmds) {
    int total = tokenizar(linea);
    if (total < 0) return -1;

    int n = 0, argc = 0;
    memset(&cmds[0], 0, sizeof cmds[0]);

    for (int i = 0; i < total; i++) {
        Token *t = &tokens[i];
        if (t->es_operador && t->texto[0] == '|') {
            if (argc == 0 || n + 1 >= MAX_COMANDOS) return -1;
            cmds[n].argv[argc] = NULL;
            n++;
            argc = 0;
            memset(&cmds[n], 0, sizeof cmds[n]);
        } else if (t->es_operador) { /* <, > o >> : el siguiente token es el archivo */
            if (i + 1 >= total || tokens[i + 1].es_operador) return -1;
            char *archivo = tokens[++i].texto;
            if (t->texto[0] == '<') {
                cmds[n].entrada = archivo;
            } else {
                cmds[n].salida = archivo;
                cmds[n].anexar = t->texto[1] == '>';
            }
        } else if (argc < MAX_ARGS - 1) {
            cmds[n].argv[argc++] = t->texto;
        }
    }
    if (argc == 0) return n == 0 && cmds[0].argv[0] == NULL ? 0 : -1;
    cmds[n].argv[argc] = NULL;
    return n + 1;
}

/* Comandos internos: devuelve 1 si lo manejo, 0 si no es interno. */
static int ejecutar_interno(Comando *c) {
    char *nombre = c->argv[0];
    if (strcmp(nombre, "exit") == 0) {
        exit(c->argv[1] ? atoi(c->argv[1]) : 0);
    }
    if (strcmp(nombre, "cd") == 0) {
        const char *destino = c->argv[1] ? c->argv[1] : getenv("HOME");
        if (destino == NULL || chdir(destino) != 0) perror("cd");
        return 1;
    }
    if (strcmp(nombre, "pwd") == 0) {
        char ruta[4096];
        if (getcwd(ruta, sizeof ruta)) puts(ruta); else perror("pwd");
        return 1;
    }
    if (strcmp(nombre, "history") == 0) {
        for (int i = 0; i < total_historial; i++) printf("%4d  %s\n", i + 1, historial[i]);
        return 1;
    }
    if (strcmp(nombre, "help") == 0) {
        puts("rsh - shell minimalista\n"
             "  Internos : cd [dir], pwd, history, help, exit [codigo]\n"
             "  Tuberias : cmd1 | cmd2 | cmd3\n"
             "  Redirecc.: cmd < entrada, cmd > salida, cmd >> salida");
        return 1;
    }
    return 0;
}

static void aplicar_redirecciones(const Comando *c) {
    if (c->entrada) {
        int fd = open(c->entrada, O_RDONLY);
        if (fd < 0) { perror(c->entrada); _exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (c->salida) {
        int flags = O_WRONLY | O_CREAT | (c->anexar ? O_APPEND : O_TRUNC);
        int fd = open(c->salida, flags, 0644);
        if (fd < 0) { perror(c->salida); _exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

/* Ejecuta una cadena de n comandos conectados por tuberias. */
static int ejecutar_tuberia(Comando *cmds, int n) {
    pid_t pids[MAX_COMANDOS];
    int entrada_previa = -1;

    for (int i = 0; i < n; i++) {
        int tubo[2] = {-1, -1};
        if (i < n - 1 && pipe(tubo) < 0) { perror("pipe"); return -1; }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return -1; }

        if (pid == 0) { /* proceso hijo */
            signal(SIGINT, SIG_DFL);
            if (entrada_previa != -1) { dup2(entrada_previa, STDIN_FILENO); close(entrada_previa); }
            if (i < n - 1) { close(tubo[0]); dup2(tubo[1], STDOUT_FILENO); close(tubo[1]); }
            aplicar_redirecciones(&cmds[i]);
            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "rsh: %s: %s\n", cmds[i].argv[0], strerror(errno));
            _exit(127);
        }

        pids[i] = pid;
        if (entrada_previa != -1) close(entrada_previa);
        if (i < n - 1) { close(tubo[1]); entrada_previa = tubo[0]; }
    }

    int estado = 0;
    for (int i = 0; i < n; i++) waitpid(pids[i], &estado, 0);
    return WIFEXITED(estado) ? WEXITSTATUS(estado) : 1;
}

int main(void) {
    char *linea = NULL;
    size_t cap = 0;
    int interactivo = isatty(STDIN_FILENO);
    Comando cmds[MAX_COMANDOS];

    signal(SIGINT, SIG_IGN); /* Ctrl+C no cierra el shell, solo al hijo */

    for (;;) {
        if (interactivo) {
            char ruta[256];
            const char *dir = getcwd(ruta, sizeof ruta) ? strrchr(ruta, '/') + 1 : "?";
            printf("\033[1;36mrsh\033[0m:\033[1;34m%s\033[0m$ ", *dir ? dir : "/");
            fflush(stdout);
        }
        if (getline(&linea, &cap, stdin) < 0) break; /* Ctrl+D */

        linea[strcspn(linea, "\n")] = '\0';
        if (linea[0] == '\0' || linea[0] == '#') continue;
        agregar_historial(linea);

        int n = analizar(linea, cmds);
        if (n < 0) { fputs("rsh: error de sintaxis\n", stderr); continue; }
        if (n == 0) continue;
        if (n == 1 && ejecutar_interno(&cmds[0])) continue;
        ejecutar_tuberia(cmds, n);
    }

    if (interactivo) putchar('\n');
    free(linea);
    for (int i = 0; i < total_historial; i++) free(historial[i]);
    return 0;
}
