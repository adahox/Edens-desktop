#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;
class QProcess;
class QProgressBar;

class DebInstaller final : public QWidget
{
public:
    explicit DebInstaller(const QString &debPath, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void loadPackageInfo();
    void startInstall();
    void setStatus(const QString &text, bool error = false);
    [[nodiscard]] QRect packageRect() const;
    [[nodiscard]] QRect targetRect() const;
    [[nodiscard]] QPixmap packageIcon() const;

    QString debPath_;
    QString packageName_;
    QString packageVersion_;
    QString packageSummary_;
    QLabel *statusLabel_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QProcess *installProcess_ = nullptr;
    QPoint dragStart_;
    bool targetHot_ = false;
    bool installing_ = false;
    bool installed_ = false;

    static constexpr int kWidth = 560;
    static constexpr int kHeight = 340;
};
