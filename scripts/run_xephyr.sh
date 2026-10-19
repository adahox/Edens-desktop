#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WINDOW_MANAGER="${PROJECT_DIR}/build/windowmanager-test"
DESKTOP="${PROJECT_DIR}/build/eden-desktop"
TARGET_DISPLAY=":2"
SCREEN="${EDEN_XEPHYR_SCREEN:-1920x1080}"

if [[ ! -x "${WINDOW_MANAGER}" || ! -x "${DESKTOP}" ]]; then
    echo "[Eden] Binários não encontrados. Compile antes:"
    echo "  cmake -S . -B build && cmake --build build"
    exit 1
fi

if [[ ! -S "/tmp/.X11-unix/X${TARGET_DISPLAY#:}" ]]; then
    echo "[Eden] Abrindo Xephyr em ${TARGET_DISPLAY} (${SCREEN})"
    Xephyr "${TARGET_DISPLAY}" -resizeable -screen "${SCREEN}" -ac >/tmp/eden-xephyr.log 2>&1 &

    for _ in 1 2 3 4 5 6 7 8 9 10; do
        if [[ -S "/tmp/.X11-unix/X${TARGET_DISPLAY#:}" ]]; then
            break
        fi
        sleep 0.2
    done
fi

if [[ ! -S "/tmp/.X11-unix/X${TARGET_DISPLAY#:}" ]]; then
    echo "[Eden] O Xephyr não abriu. Veja /tmp/eden-xephyr.log"
    exit 1
fi

stop_on_display() {
    local binary="$1"

    for pid in $(pgrep -f "${binary}" || true); do
        if [[ "${pid}" -eq "$$" ]]; then
            continue
        fi

        if tr '\0' '\n' < "/proc/${pid}/environ" 2>/dev/null | grep -qx "DISPLAY=${TARGET_DISPLAY}"; then
            kill "${pid}" 2>/dev/null || true
        fi
    done
}

stop_on_display "${WINDOW_MANAGER}"
stop_on_display "${DESKTOP}"
sleep 0.2

echo "[Eden] Subindo o gerenciador em ${TARGET_DISPLAY}"
setsid env DISPLAY="${TARGET_DISPLAY}" "${WINDOW_MANAGER}" >/tmp/eden-wm.log 2>&1 < /dev/null &
sleep 0.4

if ! DISPLAY="${TARGET_DISPLAY}" xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1; then
    echo "[Eden] O gerenciador não assumiu ${TARGET_DISPLAY}. Veja /tmp/eden-wm.log"
    exit 1
fi

echo "[Eden] Subindo o desktop em ${TARGET_DISPLAY}"
exec env DISPLAY="${TARGET_DISPLAY}" "${DESKTOP}"
