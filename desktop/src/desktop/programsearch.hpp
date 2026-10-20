#pragma once

#include <QAbstractNativeEventFilter>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QListWidget;

class ProgramSearch final : public QWidget, public QAbstractNativeEventFilter
{
public:
    explicit ProgramSearch();

    static void install();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Program
    {
        QString name;
        QString detail;
        QString haystack;
        QStringList arguments;
        QStringList matchClasses;
        QIcon icon;
        bool terminal = false;
    };

    void toggle();
    void openSearch();
    void reloadPrograms();
    void applyFilter(const QString &text);
    void launchCurrent();
    void moveSelection(int delta);
    void placeOnScreen();
    void composePlate();
    [[nodiscard]] bool isToggleSearchMessage(const void *message) const;

    QLineEdit *edit_ = nullptr;
    QListWidget *list_ = nullptr;
    QVector<Program> programs_;
    QPixmap plate_;
    mutable quint32 toggleSearchAtom_ = 0;

    static constexpr int kWidth = 560;
    static constexpr int kHeight = 460;
    static constexpr int kRadius = 16;
};
