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
    expect(area.height == 1048 && area.width == 1920, "area util desconta o painel");

    const TitleBarButtons buttons = titleBarButtons(400);
    expect(containsButton(buttons.close, buttons.close.x + 1, buttons.close.y + 1), "botao fechar");
    expect(titleButtonAt(400, 10, 10) == TitleButton::None, "clique no titulo nao e botao");

    if (failures != 0) {
        std::cerr << failures << " falha(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "Politica do gerenciador ok.\n";
    return EXIT_SUCCESS;
}
