#include "windowmanager/windowmanager.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

int main()
{
    const char *display = std::getenv("DISPLAY");
    const bool hostDisplay =
        display == nullptr ||
        display[0] == '\0' ||
        std::strcmp(display, ":0") == 0 ||
        std::strcmp(display, ":0.0") == 0;
    const bool session = std::getenv("EDEN_SESSION") != nullptr;

    if (hostDisplay && !session) {
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