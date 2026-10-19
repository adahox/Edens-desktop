
#include "desktopwindow.hpp"

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QStandardItemModel>
#include <QFileSystemWatcher>
#include <QStandardItem>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QResizeEvent>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QCloseEvent>
#include <QCursor>
#include <QShowEvent>
#include <QTimer>
#include <QApplication>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QElapsedTimer>
#include <QImage>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>
#include "filemanager/filemanagerwindow.hpp"

namespace {

class DesktopIconProvider final : public QFileIconProvider
{
public:
    DesktopIconProvider()
    {
        const QStyle *style = QApplication::style();
        directoryIcon_ = style->standardIcon(QStyle::SP_DirIcon);
        fileIcon_ = style->standardIcon(QStyle::SP_FileIcon);
    }

    QIcon icon(const QFileInfo &info) const override
    {
        return info.isDir() ? directoryIcon_ : fileIcon_;
    }

private:
    QIcon directoryIcon_;
    QIcon fileIcon_;
};

class DesktopItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(
        QPainter *painter,
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        painter->save();

        QStyleOptionViewItem itemOption = option;
        initStyleOption(&itemOption, index);

        if ((itemOption.state & QStyle::State_Selected) != 0) {
            painter->fillRect(itemOption.rect, QColor(20, 20, 20, 90));
        }

        const QSize iconSize = itemOption.decorationSize;
        const QRect iconRect(
            itemOption.rect.x() + (itemOption.rect.width() - iconSize.width()) / 2,
            itemOption.rect.y() + 6,
            iconSize.width(),
            iconSize.height());
        itemOption.icon.paint(painter, iconRect);

        const QRect textRect(
            itemOption.rect.x() + 4,
            iconRect.bottom() + 4,
            itemOption.rect.width() - 8,
            itemOption.rect.bottom() - iconRect.bottom() - 6);

        painter->setPen(QColor(0, 0, 0, 210));
        painter->drawText(
            textRect.translated(1, 1),
            Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            itemOption.text);
        painter->setPen(Qt::white);
        painter->drawText(
            textRect,
            Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
            itemOption.text);

        painter->restore();
    }
};

}  // namespace

class DesktopRipple final : public QWidget
{
public:
    explicit DesktopRipple(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        hide();
        timer_.setInterval(16);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            advance();
        });
    }

    void drop(const QPoint &position)
    {
        QWidget *desktop = parentWidget();

        if (desktop == nullptr) {
            return;
        }

        hide();
        const qreal ratio = desktop->devicePixelRatioF();
        source_ = desktop->grab().toImage().convertToFormat(QImage::Format_RGB32);
        source_.setDevicePixelRatio(1);

        if (source_.isNull()) {
            return;
        }

        origin_ = QPointF(position.x() * ratio, position.y() * ratio);
        ratio_ = ratio;
        clock_.restart();
        setGeometry(desktop->rect());
        frame_ = source_;
        raise();
        show();
        timer_.start();
        advance();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        if (frame_.isNull()) {
            return;
        }

        QPainter painter(this);
        frame_.setDevicePixelRatio(ratio_);
        painter.drawImage(QPoint(0, 0), frame_);
    }

private:
    static QRgb sample(const QImage &image, double x, double y)
    {
        const int width = image.width();
        const int height = image.height();
        x = std::clamp(x, 0.0, static_cast<double>(width - 1));
        y = std::clamp(y, 0.0, static_cast<double>(height - 1));
        const int x0 = static_cast<int>(x);
        const int y0 = static_cast<int>(y);
        const int x1 = std::min(x0 + 1, width - 1);
        const int y1 = std::min(y0 + 1, height - 1);
        const double tx = x - x0;
        const double ty = y - y0;
        const auto *row0 = reinterpret_cast<const QRgb *>(image.constScanLine(y0));
        const auto *row1 = reinterpret_cast<const QRgb *>(image.constScanLine(y1));

        const auto mix = [&](int shift) {
            const int c00 = (row0[x0] >> shift) & 255;
            const int c10 = (row0[x1] >> shift) & 255;
            const int c01 = (row1[x0] >> shift) & 255;
            const int c11 = (row1[x1] >> shift) & 255;
            const double top = c00 + (c10 - c00) * tx;
            const double bottom = c01 + (c11 - c01) * tx;
            return static_cast<int>(top + (bottom - top) * ty);
        };

        return qRgb(mix(16), mix(8), mix(0));
    }

    void advance()
    {
        constexpr double duration = 1.25;
        const double age = clock_.elapsed() / 1000.0;
        QWidget *desktop = parentWidget();

        if (desktop == nullptr || source_.isNull() || age >= duration ||
            desktop->size() != size()) {
            timer_.stop();
            hide();
            return;
        }

        frame_ = source_.copy();
        const int width = frame_.width();
        const int height = frame_.height();
        const double front = age * 860.0;
        const double fade = 1.0 - age / duration;
        const double amplitude = 22.0 * fade;
        const double band = 36.0 + age * 24.0;
        const double inner = std::max(0.0, front - band * 3.4);
        const double outer = front + band * 1.7;
        const int minY = std::clamp(static_cast<int>(origin_.y() - outer) - 2, 0, height);
        const int maxY = std::clamp(static_cast<int>(origin_.y() + outer) + 3, 0, height);
        const int minX = std::clamp(static_cast<int>(origin_.x() - outer) - 2, 0, width);
        const int maxX = std::clamp(static_cast<int>(origin_.x() + outer) + 3, 0, width);
        const double inner2 = inner * inner;
        const double outer2 = outer * outer;

        for (int y = minY; y < maxY; ++y) {
            auto *destination = reinterpret_cast<QRgb *>(frame_.scanLine(y));
            const double dy = y - origin_.y();

            for (int x = minX; x < maxX; ++x) {
                const double dx = x - origin_.x();
                const double dist2 = dx * dx + dy * dy;

                if (dist2 < inner2 || dist2 > outer2) {
                    continue;
                }

                const double dist = std::sqrt(dist2);
                const double fromFront = dist - front;
                const double envelope = std::exp(-(fromFront * fromFront) / (2.0 * band * band));
                const double trailOffset = fromFront + band * 1.35;
                const double trail = std::exp(-(trailOffset * trailOffset) / (2.0 * band * band));
                const double shape = envelope + 0.62 * trail;
                const double wave = std::sin(fromFront * 0.145) * amplitude * shape;
                const double light = std::cos(fromFront * 0.145) * 46.0 * fade * envelope;
                const double scale = dist > 0.001 ? wave / dist : 0.0;
                const QRgb pixel = sample(source_, x + dx * scale, y + dy * scale);
                destination[x] = qRgb(
                    std::clamp(qRed(pixel) + static_cast<int>(light), 0, 255),
                    std::clamp(qGreen(pixel) + static_cast<int>(light), 0, 255),
                    std::clamp(qBlue(pixel) + static_cast<int>(light * 1.08), 0, 255));
            }
        }

        if (age < 0.22) {
            QPainter painter(&frame_);
            painter.setRenderHint(QPainter::Antialiasing);
            const double splash = 1.0 - age / 0.22;
            const double radius = 8.0 + age * 110.0;
            QRadialGradient gradient(origin_, radius);
            gradient.setColorAt(0.0, QColor(255, 255, 255, static_cast<int>(160 * splash)));
            gradient.setColorAt(0.42, QColor(198, 224, 255, static_cast<int>(80 * splash)));
            gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
            painter.drawEllipse(origin_, radius, radius);
        }

        update();
    }

    QImage source_;
    QImage frame_;
    QPointF origin_;
    qreal ratio_ = 1;
    QElapsedTimer clock_;
    QTimer timer_;
};

#include <QDesktopServices>
#include <QUrl>
#include <QModelIndex>
#include <QProcess>
#include <QStringList>
#include <QDesktopServices>
#include <QUrl>
#include <QBrush>
#include <QCoreApplication>
#include <QDir>
#include <QPalette>

DesktopWindow::DesktopWindow(QWidget *parent)
    : QWidget(parent),
      desktopDirectory_(
          QStandardPaths::writableLocation(
              QStandardPaths::DesktopLocation
          )
      )
{
    if (desktopDirectory_.isEmpty()) {
        desktopDirectory_ = QDir::homePath();
    }

    QDir().mkpath(desktopDirectory_);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle("Eden Desktop");

    setupFileArea();
}

DesktopWindow::~DesktopWindow()
{
    delete iconProvider_;
    iconProvider_ = nullptr;
}

void DesktopWindow::setupFileArea()
{
    iconProvider_ = new DesktopIconProvider();
    fileModel_ = new QStandardItemModel(this);

    fileView_ = new QListView(this);
    fileView_->setItemDelegate(new DesktopItemDelegate(fileView_));

    fileView_->setModel(fileModel_);
    fileView_->setViewMode(QListView::IconMode);
    fileView_->setFlow(QListView::LeftToRight);
    fileView_->setMovement(QListView::Snap);
    fileView_->setResizeMode(QListView::Adjust);
    fileView_->setWrapping(true);

    fileView_->setIconSize(QSize(48, 48));
    fileView_->setGridSize(QSize(104, 92));
    fileView_->setSpacing(8);

    fileView_->setWordWrap(true);
    fileView_->setTextElideMode(Qt::ElideMiddle);
    fileView_->setEditTriggers(
        QAbstractItemView::NoEditTriggers
    );

    fileView_->setContextMenuPolicy(
        Qt::CustomContextMenu
    );

    fileView_->setAutoFillBackground(false);
    fileView_->viewport()->setAutoFillBackground(false);
    fileView_->setFrameShape(QFrame::NoFrame);
    fileView_->setStyleSheet(R"(
        QListView {
            border: none;
            color: #FFFFFF;
            outline: none;
            padding: 12px;
        }

        QListView::item {
            background: transparent;
            border-radius: 6px;
            padding: 4px;
        }

        QListView::item:selected {
            background: rgba(100, 150, 220, 100);
        }

        QListView::item:hover {
            background: rgba(255, 255, 255, 25);
        }
    )");

    connect(
        fileView_,
        &QListView::customContextMenuRequested,
        this,
        [this](const QPoint &position) {
            playRipple(fileView_->viewport()->mapTo(this, position));
            showDesktopContextMenu(
                fileView_->viewport()->mapToGlobal(position)
            );
        }
    );


    connect(
        fileView_,
        &QListView::doubleClicked,
        this,
        [this](const QModelIndex &index) {
            openItem(index);
        }
    );

    directoryWatcher_ = new QFileSystemWatcher(this);
    directoryWatcher_->addPath(desktopDirectory_);
    connect(
        directoryWatcher_,
        &QFileSystemWatcher::directoryChanged,
        this,
        [this](const QString &) {
            revealDesktopFiles();
        }
    );

    revealDesktopFiles();

    fileView_->setGeometry(rect());
    applyWallpaper();
}

void DesktopWindow::revealDesktopFiles()
{
    if (fileModel_ == nullptr || fileView_ == nullptr || iconProvider_ == nullptr) {
        return;
    }

    fileModel_->clear();

    const QFileInfoList entries = QDir(desktopDirectory_).entryInfoList(
        QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
        QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

    for (const QFileInfo &info : entries) {
        auto *item = new QStandardItem(iconProvider_->icon(info), info.fileName());
        item->setData(info.absoluteFilePath(), Qt::UserRole);
        item->setEditable(false);
        fileModel_->appendRow(item);
    }

    fileView_->viewport()->update();
}

void DesktopWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    revealDesktopFiles();
    applyWallpaper();
}

QString DesktopWindow::wallpaperPath() const
{
    const QDir binaryDirectory(QCoreApplication::applicationDirPath());
    const QString candidates[] = {
        binaryDirectory.filePath("../assets/wallpapers/default.jpg"),
        binaryDirectory.filePath("assets/wallpapers/default.jpg"),
        QStringLiteral("assets/wallpapers/default.jpg")
    };

    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);

        if (QFileInfo::exists(cleaned)) {
            return cleaned;
        }
    }

    return QString();
}

void DesktopWindow::applyWallpaper()
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const QPixmap source(wallpaperPath());

    if (source.isNull()) {
        wallpaper_ = QPixmap(size());
        wallpaper_.fill(QColor("#20252D"));
    } else {
        const QPixmap scaled = source.scaled(
            size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        const int x = qMax(0, (scaled.width() - width()) / 2);
        const int y = qMax(0, (scaled.height() - height()) / 2);
        wallpaper_ = scaled.copy(x, y, width(), height());
    }

    if (fileView_ != nullptr && fileView_->viewport() != nullptr) {
        QPalette palette = fileView_->viewport()->palette();
        const QBrush brush(wallpaper_);
        palette.setBrush(QPalette::Base, brush);
        palette.setBrush(QPalette::Window, brush);
        palette.setColor(QPalette::Text, QColor("#FFFFFF"));
        fileView_->viewport()->setPalette(palette);
        fileView_->viewport()->setAutoFillBackground(true);
    }

    update();
}

void DesktopWindow::openItem(const QModelIndex &index)
{
    const QString path = index.data(Qt::UserRole).toString();
    const QFileInfo info(path);

    if (info.isDir()) {
        const QString executable = QDir(
            QCoreApplication::applicationDirPath()
        ).filePath("edens");

        const bool started = QProcess::startDetached(
            executable,
            QStringList{path}
        );

        if (!started) {
            qWarning() << "Não foi possível abrir o Eden's Manager:"
                       << executable
                       << path;
        }

        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}



void DesktopWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fileView_->setGeometry(rect());
    applyWallpaper();
}


void DesktopWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    if (wallpaper_.isNull()) {
        painter.fillRect(rect(), QColor("#20252D"));
        return;
    }

    painter.drawPixmap(0, 0, wallpaper_);
}


bool DesktopWindow::sessionExitAllowed_ = false;

void DesktopWindow::allowSessionExit()
{
    sessionExitAllowed_ = true;
}

void DesktopWindow::closeEvent(QCloseEvent *event)
{
    if (sessionExitAllowed_) {
        event->accept();
        return;
    }

    event->ignore();
}

void DesktopWindow::contextMenuEvent(QContextMenuEvent *event)
{
    playRipple(event->pos());
    showDesktopContextMenu(event->globalPos());
}

void DesktopWindow::playRipple(const QPoint &localPosition)
{
    if (ripple_ == nullptr) {
        ripple_ = new DesktopRipple(this);
    }

    ripple_->drop(localPosition);
}

void DesktopWindow::showDesktopContextMenu(
    const QPoint &globalPosition
)
{
    QMenu menu(this);

    QAction *refreshAction =
        menu.addAction("Atualizar");

    QAction *terminalAction =
        menu.addAction("Abrir terminal");

    menu.addSeparator();

    QAction *settingsAction =
        menu.addAction("Configurações do Eden");

    menu.addSeparator();

    QAction *quitAction =
        menu.addAction("Encerrar sessão");

    const QPoint popupAt = globalPosition.isNull() ? QCursor::pos() : globalPosition;
    QTimer::singleShot(0, &menu, [&menu]() {
        menu.raise();
        menu.repaint();
    });
    QAction *selectedAction = menu.exec(popupAt);

    if (selectedAction == refreshAction) {
        revealDesktopFiles();
        return;
    }

    if (selectedAction == terminalAction) {
        const QStringList terminals = {
            "x-terminal-emulator",
            "gnome-terminal",
            "mate-terminal",
            "xfce4-terminal",
            "xterm"
        };

        for (const QString &terminal : terminals) {
            const QString executable =
                QStandardPaths::findExecutable(terminal);

            if (!executable.isEmpty() &&
                QProcess::startDetached(executable)) {
                return;
            }
        }

        return;
    }

    if (selectedAction == settingsAction) {
        // Implementaremos as configurações posteriormente.
        return;
    }

    if (selectedAction == quitAction) {
        allowSessionExit();
        QApplication::quit();
    }
}

void DesktopWindow::navigateToDirectory(const QString &path)
{
    if (!QFileInfo(path).isDir()) {
        return;
    }

    directoryHistory_.append(desktopDirectory_);
    desktopDirectory_ = path;

    if (directoryWatcher_ != nullptr) {
        const QStringList watched = directoryWatcher_->directories();

        if (!watched.isEmpty()) {
            directoryWatcher_->removePaths(watched);
        }

        directoryWatcher_->addPath(desktopDirectory_);
    }

    revealDesktopFiles();
}

void DesktopWindow::navigateBack()
{
    if (directoryHistory_.isEmpty()) {
        return;
    }

    desktopDirectory_ = directoryHistory_.takeLast();

    if (directoryWatcher_ != nullptr) {
        const QStringList watched = directoryWatcher_->directories();

        if (!watched.isEmpty()) {
            directoryWatcher_->removePaths(watched);
        }

        directoryWatcher_->addPath(desktopDirectory_);
    }

    revealDesktopFiles();
}
}