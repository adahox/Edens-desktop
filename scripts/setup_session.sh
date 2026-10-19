#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WINDOW_MANAGER="${PROJECT_DIR}/build/windowmanager-test"
DESKTOP="${PROJECT_DIR}/build/eden-desktop"

if [[ ! -x "${WINDOW_MANAGER}" || ! -x "${DESKTOP}" ]]; then
    echo "[Eden] Binários não encontrados. Compile antes:"
    echo "  cmake -S . -B build && cmake --build build"
    exit 1
fi

startup="$(mktemp)"
session="$(mktemp)"
trap 'rm -f "${startup}" "${session}"' EXIT

cat > "${startup}" << EOF
#!/bin/bash
export EDEN_SESSION=1
${WINDOW_MANAGER} &
sleep 0.4
exec ${DESKTOP}
EOF

cat > "${session}" << EOF
[Desktop Entry]
Name=Eden Desktop
Comment=Ambiente Eden, com gerenciador de janelas e área de trabalho
Exec=/usr/local/bin/start-eden
Type=Application
DesktopNames=Eden
EOF

echo "[Eden] Instalando /usr/local/bin/start-eden"
sudo install -m 755 "${startup}" /usr/local/bin/start-eden

echo "[Eden] Registrando /usr/share/xsessions/eden.desktop"
sudo install -m 644 "${session}" /usr/share/xsessions/eden.desktop

echo "[Eden] Pronto. Saia da sessão atual e escolha Eden Desktop na tela de login."
