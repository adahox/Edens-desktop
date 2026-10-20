#include "installer/debinstaller.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QScreen>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    qunsetenv("QT_QPA_PLATFORMTHEME");
    qputenv("QT_STYLE_OVERRIDE", "Fusion");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Eden Installer"));

    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }

    if (argc < 2) {
        QMessageBox::information(
            nullptr,
            QStringLiteral("Instalador Eden"),
            QStringLiteral("Abra um arquivo .deb para instalar.\n\nUso: eden-installer pacote.deb"));
        return 1;
    }

    const QString path = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();

    if (!QFileInfo::exists(path) || !path.endsWith(QLatin1String(".deb"), Qt::CaseInsensitive)) {
        QMessageBox::warning(
            nullptr,
            QStringLiteral("Instalador Eden"),
            QStringLiteral("Arquivo .deb inválido:\n%1").arg(path));
        return 1;
    }

    DebInstaller window(path);

    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        window.move(
            area.left() + (area.width() - window.width()) / 2,
            area.top() + (area.height() - window.height()) / 2);
    }

    window.show();
    return app.exec();
}
