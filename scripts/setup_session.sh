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
${WINDOW_MANAGER} >/tmp/eden-wm.log 2>&1 &
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

if ! command -v Xorg >/dev/null 2>&1; then
    echo "[Eden] Xorg não encontrado. O Eden é uma sessão X11."
    echo "  Instale com: sudo apt install xserver-xorg xinit"
    exit 1
fi

echo "[Eden] Instalando /usr/local/bin/start-eden"
sudo install -d /usr/local/bin
sudo install -m 755 "${startup}" /usr/local/bin/start-eden

echo "[Eden] Registrando /usr/share/xsessions/eden.desktop"
sudo install -d /usr/share/xsessions
sudo install -m 644 "${session}" /usr/share/xsessions/eden.desktop

echo "[Eden] Pronto. Saia da sessão atual e escolha Eden Desktop na tela de login."
echo "[Eden] No GDM, use o seletor de sessão (ícone de engrenagem) — a sessão Eden aparece em X11."
