#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WINDOW_MANAGER="${PROJECT_DIR}/build/windowmanager-test"
DESKTOP="${PROJECT_DIR}/build/eden-desktop"
INSTALLER="${PROJECT_DIR}/build/eden-installer"

if [[ ! -x "${WINDOW_MANAGER}" || ! -x "${DESKTOP}" || ! -x "${INSTALLER}" ]]; then
    echo "[Eden] Binários não encontrados. Compile antes:"
    echo "  cmake -S . -B build && cmake --build build"
    exit 1
fi

startup="$(mktemp)"
session="$(mktemp)"
mime="$(mktemp)"
trap 'rm -f "${startup}" "${session}" "${mime}"' EXIT

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

cat > "${mime}" << EOF
[Desktop Entry]
Type=Application
Name=Instalador Eden
Comment=Instala pacotes .deb arrastando para o Sistema
Exec=/usr/local/bin/eden-installer %f
Icon=system-software-install
Terminal=false
Categories=System;PackageManager;
MimeType=application/vnd.debian.binary-package;
NoDisplay=false
EOF

if ! command -v Xorg >/dev/null 2>&1; then
    echo "[Eden] Xorg não encontrado. O Eden é uma sessão X11."
    echo "  Instale com: sudo apt install xserver-xorg xinit"
    exit 1
fi

echo "[Eden] Instalando /usr/local/bin/start-eden"
sudo install -d /usr/local/bin
sudo install -m 755 "${startup}" /usr/local/bin/start-eden

echo "[Eden] Instalando /usr/local/bin/eden-installer"
sudo install -m 755 "${INSTALLER}" /usr/local/bin/eden-installer

echo "[Eden] Registrando /usr/share/xsessions/eden.desktop"
sudo install -d /usr/share/xsessions
sudo install -m 644 "${session}" /usr/share/xsessions/eden.desktop

echo "[Eden] Registrando handler de .deb"
sudo install -d /usr/share/applications
sudo install -m 644 "${mime}" /usr/share/applications/eden-installer.desktop
sudo update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
xdg-mime default eden-installer.desktop application/vnd.debian.binary-package >/dev/null 2>&1 || true

echo "[Eden] Pronto. Saia da sessão atual e escolha Eden Desktop na tela de login."
echo "[Eden] No GDM, use o seletor de sessão (ícone de engrenagem) — a sessão Eden aparece em X11."
echo "[Eden] Arquivos .deb abrem no Instalador Eden (arraste para o Sistema)."
