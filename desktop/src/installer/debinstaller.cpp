#include "installer/debinstaller.hpp"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProgressBar>
#include <QLinearGradient>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QStyle>

DebInstaller::DebInstaller(const QString &debPath, QWidget *parent)
    : QWidget(parent),
      debPath_(debPath)
{
    setWindowTitle(QStringLiteral("Instalador Eden"));
    setFixedSize(kWidth, kHeight);
    setAcceptDrops(true);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, kHeight - 78, 28, 22);
    layout->setSpacing(8);
    layout->addStretch();

    statusLabel_ = new QLabel(QStringLiteral("Arraste o pacote para o Sistema para instalar"), this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setStyleSheet(QStringLiteral("color: #3A3A3C; background: transparent; font-size: 13px;"));
    layout->addWidget(statusLabel_);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    progress_->setVisible(false);
    progress_->setStyleSheet(QStringLiteral(
        "QProgressBar { background: #D2D2D7; border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: #007AFF; border-radius: 3px; }"));
    layout->addWidget(progress_);

    loadPackageInfo();
}

void DebInstaller::loadPackageInfo()
{
    const QFileInfo info(debPath_);
    packageName_ = info.completeBaseName();
    packageVersion_.clear();
    packageSummary_.clear();

    QProcess process;
    process.start(
        QStringLiteral("dpkg-deb"),
        {QStringLiteral("-f"), debPath_, QStringLiteral("Package"), QStringLiteral("Version"),
         QStringLiteral("Description")});

    if (!process.waitForFinished(4000) || process.exitCode() != 0) {
        setStatus(QStringLiteral("Pacote: %1").arg(info.fileName()));
        return;
    }

    const QStringList lines =
        QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    if (lines.size() >= 1 && !lines.at(0).trimmed().isEmpty()) {
        packageName_ = lines.at(0).trimmed();
    }

    if (lines.size() >= 2) {
        packageVersion_ = lines.at(1).trimmed();
    }

    if (lines.size() >= 3) {
        packageSummary_ = lines.at(2).trimmed();
    }

    setStatus(
        packageVersion_.isEmpty()
            ? QStringLiteral("Arraste “%1” para o Sistema para instalar").arg(packageName_)
            : QStringLiteral("Arraste “%1” (%2) para o Sistema para instalar")
                  .arg(packageName_, packageVersion_));
}

QRect DebInstaller::packageRect() const
{
    return QRect(78, 78, 120, 140);
}

QRect DebInstaller::targetRect() const
{
    return QRect(width() - 78 - 120, 78, 120, 140);
}

QPixmap DebInstaller::packageIcon() const
{
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient gradient(0, 0, 96, 96);
    gradient.setColorAt(0.0, QColor(90, 200, 250));
    gradient.setColorAt(1.0, QColor(0, 122, 255));
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(8, 8, 80, 80, 20, 20);

    painter.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(34, 48), QPointF(48, 62));
    painter.drawLine(QPointF(48, 62), QPointF(66, 36));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 40));
    painter.drawRoundedRect(14, 14, 68, 28, 12, 12);

    return pixmap;
}

void DebInstaller::setStatus(const QString &text, bool error)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(
        error ? QStringLiteral("color: #D70015; background: transparent; font-size: 13px;")
              : QStringLiteral("color: #3A3A3C; background: transparent; font-size: 13px;"));
}

void DebInstaller::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QLinearGradient background(0, 0, 0, height());
    background.setColorAt(0.0, QColor(245, 245, 247));
    background.setColorAt(1.0, QColor(232, 232, 237));
    painter.fillRect(rect(), background);

    painter.setPen(QColor(210, 210, 215));
    painter.drawLine(0, height() - 86, width(), height() - 86);

    const QRect package = packageRect();
    const QRect target = targetRect();

    painter.drawPixmap(
        package.center().x() - 48,
        package.top() + 4,
        packageIcon());

    QFont nameFont = font();
    nameFont.setPointSize(12);
    nameFont.setWeight(QFont::DemiBold);
    painter.setFont(nameFont);
    painter.setPen(QColor(28, 28, 30));
    painter.drawText(
        QRect(package.left() - 20, package.bottom() - 34, package.width() + 40, 20),
        Qt::AlignCenter,
        fontMetrics().elidedText(packageName_, Qt::ElideRight, package.width() + 36));

    if (!packageVersion_.isEmpty()) {
        QFont versionFont = font();
        versionFont.setPointSize(10);
        painter.setFont(versionFont);
        painter.setPen(QColor(99, 99, 102));
        painter.drawText(
            QRect(package.left() - 20, package.bottom() - 16, package.width() + 40, 18),
            Qt::AlignCenter,
            packageVersion_);
    }

    const QPoint arrowStart(package.right() + 18, package.center().y() - 10);
    const QPoint arrowEnd(target.left() - 18, package.center().y() - 10);
    painter.setPen(QPen(QColor(142, 142, 147), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(arrowStart, arrowEnd);
    QPainterPath tip;
    tip.moveTo(arrowEnd);
    tip.lineTo(arrowEnd.x() - 12, arrowEnd.y() - 8);
    tip.lineTo(arrowEnd.x() - 12, arrowEnd.y() + 8);
    tip.closeSubpath();
    painter.setBrush(QColor(142, 142, 147));
    painter.setPen(Qt::NoPen);
    painter.drawPath(tip);

    QColor targetFill = targetHot_ ? QColor(0, 122, 255, 28) : QColor(255, 255, 255, 180);
    QColor targetBorder = targetHot_ ? QColor(0, 122, 255) : QColor(174, 174, 178);
    painter.setBrush(targetFill);
    painter.setPen(QPen(targetBorder, targetHot_ ? 2.5 : 1.6, Qt::DashLine, Qt::RoundCap));
    painter.drawRoundedRect(target.adjusted(8, 0, -8, -28), 28, 28);

    QPixmap systemIcon = style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(64, 64);
    painter.drawPixmap(target.center().x() - 32, target.top() + 22, systemIcon);

    QFont targetFont = font();
    targetFont.setPointSize(12);
    targetFont.setWeight(QFont::DemiBold);
    painter.setFont(targetFont);
    painter.setPen(targetHot_ ? QColor(0, 90, 200) : QColor(28, 28, 30));
    painter.drawText(
        QRect(target.left() - 10, target.bottom() - 34, target.width() + 20, 22),
        Qt::AlignCenter,
        installed_ ? QStringLiteral("Instalado") : QStringLiteral("Sistema"));

    if (installed_) {
        painter.setBrush(QColor(50, 215, 75));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(target.right() - 28, target.top() + 8, 22, 22);
        painter.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(target.right() - 22, target.top() + 19, target.right() - 18, target.top() + 23);
        painter.drawLine(target.right() - 18, target.top() + 23, target.right() - 12, target.top() + 14);
    }
}

void DebInstaller::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && packageRect().contains(event->position().toPoint()) &&
        !installing_ && !installed_) {
        dragStart_ = event->position().toPoint();
    }

    QWidget::mousePressEvent(event);
}

void DebInstaller::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || installing_ || installed_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if ((event->position().toPoint() - dragStart_).manhattanLength() < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!packageRect().contains(dragStart_)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(debPath_)});
    mime->setData(QStringLiteral("application/x-eden-deb"), debPath_.toUtf8());

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(packageIcon().scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    drag->setHotSpot(QPoint(36, 36));
    drag->exec(Qt::CopyAction);
}

void DebInstaller::dragEnterEvent(QDragEnterEvent *event)
{
    if (installing_ || installed_) {
        return;
    }

    if (event->mimeData()->hasFormat(QStringLiteral("application/x-eden-deb")) ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DebInstaller::dragMoveEvent(QDragMoveEvent *event)
{
    if (installing_ || installed_) {
        return;
    }

    const bool hot = targetRect().contains(event->position().toPoint());

    if (hot != targetHot_) {
        targetHot_ = hot;
        update();
    }

    if (hot) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DebInstaller::dragLeaveEvent(QDragLeaveEvent *)
{
    if (targetHot_) {
        targetHot_ = false;
        update();
    }
}

void DebInstaller::dropEvent(QDropEvent *event)
{
    targetHot_ = false;
    update();

    if (installing_ || installed_ || !targetRect().contains(event->position().toPoint())) {
        return;
    }

    event->acceptProposedAction();
    startInstall();
}

void DebInstaller::startInstall()
{
    if (installing_ || installed_) {
        return;
    }

    installing_ = true;
    progress_->setVisible(true);
    setStatus(QStringLiteral("Instalando %1...").arg(packageName_));
    update();

    if (installProcess_ != nullptr) {
        installProcess_->deleteLater();
    }

    installProcess_ = new QProcess(this);
    connect(installProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        progress_->setVisible(false);
        installing_ = false;

        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString error = QString::fromUtf8(installProcess_->readAllStandardError()).trimmed();
            setStatus(
                error.isEmpty()
                    ? QStringLiteral("Falha na instalação. Verifique permissões (pkexec).")
                    : error,
                true);
            update();
            return;
        }

        installed_ = true;
        setStatus(QStringLiteral("%1 instalado com sucesso").arg(packageName_));
        update();
    });

    const QString absolute = QFileInfo(debPath_).absoluteFilePath();
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    const QString apt = QStandardPaths::findExecutable(QStringLiteral("apt-get"));
    const QString dpkg = QStandardPaths::findExecutable(QStringLiteral("dpkg"));

    if (!pkexec.isEmpty() && !apt.isEmpty()) {
        installProcess_->start(
            pkexec,
            {apt, QStringLiteral("install"), QStringLiteral("-y"), absolute});
        return;
    }

    if (!pkexec.isEmpty() && !dpkg.isEmpty()) {
        installProcess_->start(pkexec, {dpkg, QStringLiteral("-i"), absolute});
        return;
    }

    installing_ = false;
    progress_->setVisible(false);
    setStatus(QStringLiteral("Não encontrei pkexec/apt-get para instalar."), true);
    update();
}
