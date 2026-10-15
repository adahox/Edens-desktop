Eden

Projeto de ambiente desktop Linux desenvolvido com C, C++ e Qt.

Componentes

Eden Desktop

Ambiente de desktop desenvolvido em C++ e Qt 6, responsável pela área de trabalho, papel de parede e ícones.

Eden's Manager

Gerenciador de arquivos desenvolvido em C e GTK 3, responsável pela navegação entre diretórios.

Tecnologias

C11

C++20

Qt 6 Widgets

GTK 3

CMake

Compilação

cmake -S . -B build
cmake --build build

Execução

Iniciar o desktop:

./build/eden-desktop

Iniciar o gerenciador de arquivos diretamente:

./build/edens "$HOME"