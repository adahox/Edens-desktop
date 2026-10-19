#include "windowmanager/windowmanager.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ios>

namespace {

bool environmentIs(const char *name, const char *expected)
{
    const char *value = std::getenv(name);
    return value != nullptr && std::strcmp(value, expected) == 0;
}

bool edenSession()
{
    return std::getenv("EDEN_SESSION") != nullptr ||
           environmentIs("XDG_CURRENT_DESKTOP", "Eden") ||
           environmentIs("XDG_CURRENT_DESKTOP", "eden") ||
           environmentIs("DESKTOP_SESSION", "eden") ||
           environmentIs("XDG_SESSION_DESKTOP", "eden");
}

} // namespace

int main()
{
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    const char *display = std::getenv("DISPLAY");
    const bool hostDisplay =
        display == nullptr ||
        display[0] == '\0' ||
        std::strcmp(display, ":0") == 0 ||
        std::strcmp(display, ":0.0") == 0;

    if (hostDisplay && !edenSession()) {
        std::cerr
            << "O gerenciador não vai assumir o display :0 a partir deste terminal.\n"
            << "Isso evita conflito com o gerenciador do Mint.\n"
            << "Para testar, use ./scripts/run_xephyr.sh\n";
        return 1;
    }

    WindowManager windowManager;

    if (!windowManager.connect()) {
        std::cerr << "Falha ao conectar ao servidor X11.\n";
        return 1;
    }

    std::cout << "Conexão X11 estabelecida com sucesso.\n";

    if (!windowManager.acquireControl()) {
        std::cerr
            << "Não foi possível assumir o controle da janela raiz.\n"
            << "Outro gerenciador pode estar ativo nesta tela.\n";

        return 1;
    }

    std::cout << "Controle da janela raiz adquirido com sucesso.\n";


    windowManager.run();
    return 0;
}