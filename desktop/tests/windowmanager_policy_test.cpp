#include "windowmanager/windowmanagerpolicy.hpp"
#include "windowmanager/titlebarlayout.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        ++failures;
    }
}

}  // namespace

int main()
{
    const FrameSize clamped = clampFrameSize(40, 40, 150, 100, 800, 600, true);
    expect(clamped.width == 150 && clamped.height == 100, "minimo nao aplicado");

    const FrameSize expanded = clampFrameSize(2000, 2000, 150, 100, 800, 600, true);
    expect(expanded.width == 800 && expanded.height == 600, "maximo nao aplicado");

    const FrameSize maximized = clampFrameSize(2000, 2000, 150, 100, 800, 600, false);
    expect(maximized.width == 2000 && maximized.height == 2000, "maximizar deve ignorar o maximo");

    expect(nextCycleIndex(-1, 3, 1) == 0, "ciclo inicial");
    expect(nextCycleIndex(0, 3, 1) == 1, "proximo");
    expect(nextCycleIndex(2, 3, 1) == 0, "volta ao inicio");
    expect(nextCycleIndex(0, 3, -1) == 2, "anterior");
    expect(nextCycleIndex(-1, 0, 1) == -1, "lista vazia");

    const std::vector<MonitorRect> monitors = {
        {0, 0, 1920, 1080},
        {1920, 0, 1280, 1024}
    };

    expect(monitorIndexAt(monitors, 10, 10) == 0, "primeiro monitor");
    expect(monitorIndexAt(monitors, 2000, 20) == 1, "segundo monitor");
    expect(monitorIndexAt(monitors, -10, -10) == 0, "fora da tela usa o primeiro");

    const MonitorRect area = workAreaFor(monitors[0], 32);
    expect(area.y == 32 && area.height == 1048 && area.width == 1920, "area util desconta o painel");

    const MonitorRect primaryArea = clampedWorkArea(monitors[0], 3200, 1080, 0, 0, 28, 64);
    expect(
        primaryArea.x == 0 && primaryArea.y == 28 &&
            primaryArea.width == 1920 && primaryArea.height == 988,
        "principal desconta barra e dock");

    // Secondary monitors ignore global struts: full glass for apps.
    const MonitorRect second = monitors[1];
    expect(
        second.x == 1920 && second.y == 0 &&
            second.width == 1280 && second.height == 1024,
        "secundario fica livre");

    const TitleBarButtons buttons = titleBarButtons(400);
    expect(containsButton(buttons.close, buttons.close.x + 1, buttons.close.y + 1), "botao fechar");
    expect(titleButtonAt(400, 10, 10) == TitleButton::None, "clique no titulo nao e botao");

    expect(motifWantsTitleBar(false, 0), "sem flag motif mantem barra");
    expect(!motifWantsTitleBar(true, 0), "motif decor 0 remove barra");
    expect(motifWantsTitleBar(true, kMotifDecorAll), "motif all mantem barra");
    expect(!motifWantsTitleBar(true, 1u << 1), "motif so borda remove barra");
    expect(clientTopOffset(true) == kTitleBarHeight, "chrome com barra");
    expect(clientTopOffset(false) == 0, "chrome sem barra");
    expect(frameHeightForClient(600, true) == 600 + kTitleBarHeight, "altura com barra");
    expect(frameHeightForClient(600, false) == 600, "altura sem barra");

    expect(resizeDirectionForNetWm(0) == ResizeDirection::TopLeft, "moveresize canto");
    expect(resizeDirectionForNetWm(5) == ResizeDirection::Bottom, "moveresize base");
    expect(resizeDirectionForNetWm(8) == ResizeDirection::None, "moveresize mover nao e borda");
    expect(netWmMoveResizeIsMove(8) && netWmMoveResizeIsMove(10), "moveresize mover");
    expect(!netWmMoveResizeIsMove(4), "redimensionar nao e mover");
    expect(netWmMoveResizeIsCancel(11), "moveresize cancelar");

    if (failures != 0) {
        std::cerr << failures << " falha(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "Politica do gerenciador ok.\n";
    return EXIT_SUCCESS;
}
