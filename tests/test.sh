#!/bin/sh
# Pruebas de integracion: envia comandos al shell y compara la salida.
RSH=${RSH:-./rsh}
TMP=$(mktemp -d)
pasaron=0
fallaron=0

probar() {
    nombre=$1; entrada=$2; esperado=$3
    obtenido=$(printf '%s\n' "$entrada" | "$RSH" 2>&1)
    if [ "$obtenido" = "$esperado" ]; then
        pasaron=$((pasaron + 1)); echo "  ok   $nombre"
    else
        fallaron=$((fallaron + 1)); echo "  FALLO $nombre"
        echo "        esperado: $esperado"; echo "        obtenido: $obtenido"
    fi
}

probar "comando simple"     "echo hola mundo"                       "hola mundo"
probar "tuberia doble"      "printf 'b\na\nc\n' | sort | head -n 1" "a"
probar "tuberia triple"     "printf 'x\ny\nz\n' | grep -v y | wc -l" "2"
probar "redireccion salida" "echo dato > $TMP/f.txt
cat $TMP/f.txt"                                                     "dato"
probar "append"             "echo uno > $TMP/g.txt
echo dos >> $TMP/g.txt
wc -l < $TMP/g.txt"                                                 "2"
probar "cd y pwd"           "cd $TMP
pwd"                                                                "$TMP"
probar "comando inexistente" "comando_que_no_existe" "rsh: comando_que_no_existe: No such file or directory"
probar "comillas"          'echo "a   b" | cat'                   "a   b"
probar "operador pegado"   "printf '1\n2\n'|wc -l"                 "2"
probar "comilla sin cerrar" 'echo "hola'                           "rsh: error de sintaxis"
probar "error de sintaxis"  "ls |"                                  "rsh: error de sintaxis"

rm -rf "$TMP"
echo ""
echo "$pasaron pasaron, $fallaron fallaron"
[ "$fallaron" -eq 0 ]
