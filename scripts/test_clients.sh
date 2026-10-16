#!/bin/bash
set -euo pipefail

if [[ "${DISPLAY:-}" != :[2-9]* && "${DISPLAY:-}" != :[1-9][0-9]* ]]; then
    echo "[Eden] Defina DISPLAY para um Xephyr, por exemplo DISPLAY=:2."
    echo "[Eden] Este script não abre clientes no display :0."
    exit 1
fi

echo "[Eden] Display ${DISPLAY}"
echo "[Eden] Suba antes o gerenciador: ./build/windowmanager-test"
echo

launch() {
    local name="$1"
    shift

    if command -v "$name" >/dev/null 2>&1; then
        echo "[Eden] Abrindo ${name}"
        "$@" >/dev/null 2>&1 &
    else
        echo "[Eden] Ausente: ${name}"
    fi
}

launch xeyes xeyes
launch xclock xclock
launch gtk3-demo gtk3-demo
launch gtk4-demo gtk4-demo
launch qt6-demo qt6-demo

if command -v firefox >/dev/null 2>&1; then
    echo "[Eden] Firefox está instalado. Abra manualmente se quiser testar o navegador."
elif command -v chromium >/dev/null 2>&1 || command -v chromium-browser >/dev/null 2>&1; then
    echo "[Eden] Chromium está instalado. Abra manualmente se quiser testar o navegador."
else
    echo "[Eden] Nenhum navegador encontrado no PATH."
fi
