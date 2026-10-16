#include "windowmanager/windowmanager.hpp"

#include <iostream>

int main()
{
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