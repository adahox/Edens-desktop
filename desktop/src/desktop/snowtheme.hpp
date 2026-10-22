#pragma once

#include "desktop/glassstyle.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>

namespace EdenSnow {

inline void apply(QApplication *app)
{
    if (app == nullptr) {
        return;
    }

    QFont font(QStringLiteral("Cantarell"));

    if (!QFontDatabase::hasFamily(QStringLiteral("Cantarell"))) {
        font = QFont(QStringLiteral("Inter"));
    }

    if (!QFontDatabase::hasFamily(font.family())) {
        font = QFont(QStringLiteral("Sans"));
    }

    font.setPointSize(11);
    font.setStyleStrategy(QFont::PreferAntialias);
    app->setFont(font);

    const EdenGlass::Appearance look = EdenGlass::current();
    const QString panel = look.windowColor().name(QColor::HexArgb);
    const QString text = look.textColor().name(QColor::HexArgb);
    const QString muted = look.borderColor().name(QColor::HexArgb);
    const QString field = look.dark
        ? QStringLiteral("rgba(255, 255, 255, 0.06)")
        : QStringLiteral("rgba(255, 255, 255, 0.72)");

    app->setStyleSheet(QStringLiteral(R"(
        QMenu, QToolTip, QMessageBox, QDialog {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 14px;
        }
        QMenu { padding: 6px; }
        QMenu::item {
            padding: 8px 22px;
            border-radius: 8px;
            background: transparent;
            color: %2;
        }
        QMenu::item:selected {
            background: rgba(0, 229, 255, 0.18);
            color: #00E5FF;
        }
        QMenu::separator {
            height: 1px;
            background: %3;
            margin: 6px 8px;
        }
        QToolTip { padding: 8px 10px; }
        QLabel { color: %2; }
        QLineEdit, QComboBox, QSpinBox, QTextEdit, QPlainTextEdit {
            background: %4;
            color: %2;
            border: 1px solid %3;
            border-radius: 12px;
            padding: 6px 10px;
            selection-background-color: #00E5FF;
            selection-color: #121820;
        }
        QPushButton {
            background: %4;
            color: %2;
            border: 1px solid %3;
            border-radius: 12px;
            padding: 8px 14px;
        }
        QPushButton:hover { background: rgba(0, 229, 255, 0.16); }
        QPushButton:pressed { background: rgba(0, 229, 255, 0.28); }
        QScrollBar:vertical, QScrollBar:horizontal {
            background: transparent;
            width: 8px;
            height: 8px;
            margin: 2px;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: rgba(0, 229, 255, 0.45);
            border-radius: 4px;
            min-height: 24px;
            min-width: 24px;
        }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )").arg(panel, text, muted, field));
}

}  // namespace EdenSnow
