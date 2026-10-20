
#include "desktopwindow.hpp"
#include "desktop/desktoplaunch.hpp"
#include "desktop/dockbar.hpp"
#include "desktop/wallpapersettings.hpp"

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QStandardItemModel>
#include <QFileSystemWatcher>
#include <QStandardItem>
#include <QListView>
#include <QMenu>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QShowEvent>
#include <QTextStream>
#include <QTimer>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QRadialGradient>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include "filemanager/filemanagerwindow.hpp"

namespace {

constexpr int kDesktopIconSize = 64;
constexpr int kDesktopGridW = 112;
constexpr int kDesktopGridH = 108;
constexpr int kDesktopPad = 16;

class DesktopIconProvider final : public QFileIconProvider
{
public:
    DesktopIconProvider()
    {
        const QStyle *style = QApplication::style();
        directoryIcon_ = QIcon::fromTheme(
            QStringLiteral("folder"),
            style->standardIcon(QStyle::SP_DirIcon));
        fileIcon_ = QIcon::fromTheme(
            QStringLiteral("text-x-generic"),
            style->standardIcon(QStyle::SP_FileIcon));
    }

    [[nodiscard]] bool isImage(const QFileInfo &info) const
    {
        if (!info.isFile()) {
            return false;
        }

        const QMimeType mime = mimeDb_.mimeTypeForFile(info);
        if (mime.name().startsWith(QLatin1String("image/"))) {
            return true;
        }

        static const QSet<QString> suffixes = {
            QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
            QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("svg"),
        };
        return suffixes.contains(info.suffix().toLower());
    }

    QIcon icon(const QFileInfo &info) const override
    {
        if (info.isDir()) {
            return directoryIcon_;
        }

        if (info.suffix().compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0) {
            const QIcon desktopIcon =
                EdenDesktop::iconFor(EdenDesktop::fields(info.absoluteFilePath()));

            if (!desktopIcon.isNull()) {
                return desktopIcon;
            }
        }

        if (isImage(info)) {
            const QString cacheKey = info.absoluteFilePath()
                + QLatin1Char('|')
                + QString::number(info.lastModified().toSecsSinceEpoch());

            if (thumbnailCache_.contains(cacheKey)) {
                return thumbnailCache_.value(cacheKey);
            }

            QPixmap pixmap(info.absoluteFilePath());

            if (!pixmap.isNull()) {
                const QPixmap thumb = pixmap.scaled(
                    kDesktopIconSize,
                    kDesktopIconSize,
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation);
                const QIcon thumbIcon(thumb);
                thumbnailCache_.insert(cacheKey, thumbIcon);
                return thumbIcon;
            }
        }

        const QMimeType mime = mimeDb_.mimeTypeForFile(info);
        const QStringList themeNames = {
            mime.iconName(),
            mime.genericIconName(),
        };

        for (const QString &name : themeNames) {
            if (name.isEmpty()) {
                continue;
            }

            const QIcon themed = QIcon::fromTheme(name);

            if (!themed.isNull()) {
                return themed;
            }
        }

        const QIcon systemIcon = QFileIconProvider::icon(info);

        if (!systemIcon.isNull()) {
            return systemIcon;
        }

        return fileIcon_;
    }

private:
    QIcon directoryIcon_;
    QIcon fileIcon_;
    mutable QMimeDatabase mimeDb_;
    mutable QHash<QString, QIcon> thumbnailCache_;
};

class DesktopListView final : public QListView
{
public:
    using QListView::QListView;

    void placeItem(const QPoint &position, const QModelIndex &index)
    {
        setPositionForIndex(position, index);
    }
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
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        QStyleOptionViewItem itemOption = option;
        initStyleOption(&itemOption, index);

        if ((itemOption.state & QStyle::State_Selected) != 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(20, 20, 20, 100));
            painter->drawRoundedRect(itemOption.rect.adjusted(2, 2, -2, -2), 10, 10);
        } else if ((itemOption.state & QStyle::State_MouseOver) != 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 28));
            painter->drawRoundedRect(itemOption.rect.adjusted(2, 2, -2, -2), 10, 10);
        }

        const QSize iconSize = itemOption.decorationSize;
        const QRect iconRect(
            itemOption.rect.x() + (itemOption.rect.width() - iconSize.width()) / 2,
            itemOption.rect.y() + 6,
            iconSize.width(),
            iconSize.height());

        const bool isImage = index.data(Qt::UserRole + 1).toBool();
        const QPixmap pixmap = itemOption.icon.pixmap(iconSize);

        if (isImage && !pixmap.isNull()) {
            const QPixmap fitted = pixmap.scaled(
                iconSize,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
            const QRect drawRect(
                iconRect.x() + (iconRect.width() - fitted.width()) / 2,
                iconRect.y() + (iconRect.height() - fitted.height()) / 2,
                fitted.width(),
                fitted.height());

            QPainterPath clip;
            clip.addRoundedRect(QRectF(drawRect).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
            painter->save();
            painter->setClipPath(clip);
            painter->drawPixmap(drawRect, fitted);
            painter->restore();
            painter->setPen(QPen(QColor(255, 255, 255, 55), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(drawRect.adjusted(0, 0, -1, -1), 8, 8);
        } else {
            itemOption.icon.paint(painter, iconRect);
        }

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

    auto *desktopView = new DesktopListView(this);
    fileView_ = desktopView;
    fileView_->setItemDelegate(new DesktopItemDelegate(fileView_));

    fileView_->setModel(fileModel_);
    fileView_->setViewMode(QListView::IconMode);
    fileView_->setFlow(QListView::LeftToRight);
    fileView_->setMovement(QListView::Free);
    fileView_->setResizeMode(QListView::Fixed);
    fileView_->setWrapping(true);
    fileView_->setUniformItemSizes(false);

    fileView_->setIconSize(QSize(kDesktopIconSize, kDesktopIconSize));
    fileView_->setGridSize(QSize(kDesktopGridW, kDesktopGridH));
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

    connect(
        fileView_,
        &QListView::indexesMoved,
        this,
        [this](const QModelIndexList &) {
            if (!restoringLayout_) {
                scheduleSaveDesktopLayout();
            }
        });

    layoutSaveTimer_ = new QTimer(this);
    layoutSaveTimer_->setSingleShot(true);
    layoutSaveTimer_->setInterval(250);
    connect(layoutSaveTimer_, &QTimer::timeout, this, [this]() {
        saveDesktopLayout();
    });

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

    fileView_->viewport()->installEventFilter(this);
    fileView_->setGeometry(rect());
    applyWallpaper();
}

QString DesktopWindow::layoutConfigPath() const
{
    const QString dir = QDir::home().filePath(QStringLiteral(".config/eden"));
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("desktop-layout.conf"));
}

QHash<QString, QPoint> DesktopWindow::loadDesktopLayout() const
{
    QHash<QString, QPoint> positions;
    QFile file(layoutConfigPath());

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return positions;
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const int split = line.indexOf(QLatin1Char('='));

        if (split <= 0) {
            continue;
        }

        const QString key = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();
        const QStringList parts = value.split(QLatin1Char(','));

        if (parts.size() != 2) {
            continue;
        }

        bool okX = false;
        bool okY = false;
        const int x = parts.at(0).toInt(&okX);
        const int y = parts.at(1).toInt(&okY);

        if (okX && okY) {
            positions.insert(key, QPoint(x, y));
        }
    }

    return positions;
}

void DesktopWindow::scheduleSaveDesktopLayout()
{
    if (layoutSaveTimer_ != nullptr) {
        layoutSaveTimer_->start();
    }
}

void DesktopWindow::saveDesktopLayout()
{
    if (fileModel_ == nullptr || fileView_ == nullptr || restoringLayout_) {
        return;
    }

    QSaveFile file(layoutConfigPath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << "# Eden desktop icon positions\n";

    for (int row = 0; row < fileModel_->rowCount(); ++row) {
        const QModelIndex index = fileModel_->index(row, 0);
        const QString path = index.data(RolePath).toString();

        if (path.isEmpty()) {
            continue;
        }

        const QPoint topLeft = fileView_->visualRect(index).topLeft();
        stream << QFileInfo(path).fileName() << '=' << topLeft.x() << ','
               << topLeft.y() << '\n';
    }

    file.commit();
}

QPoint DesktopWindow::nextFreeIconPosition(const QHash<QString, QPoint> &used) const
{
    QSet<QPoint> occupied;

    for (auto it = used.cbegin(); it != used.cend(); ++it) {
        occupied.insert(it.value());
    }

    const int viewportW = fileView_ != nullptr
        ? std::max(kDesktopGridW * 2, fileView_->viewport()->width() - kDesktopPad)
        : 800;

    for (int y = kDesktopPad; y < 4000; y += kDesktopGridH) {
        for (int x = kDesktopPad; x + kDesktopGridW <= viewportW; x += kDesktopGridW) {
            const QPoint candidate(x, y);

            if (!occupied.contains(candidate)) {
                return candidate;
            }
        }
    }

    return QPoint(kDesktopPad, kDesktopPad);
}

void DesktopWindow::revealDesktopFiles()
{
    if (fileModel_ == nullptr || fileView_ == nullptr || iconProvider_ == nullptr) {
        return;
    }

    saveDesktopLayout();

    const QHash<QString, QPoint> saved = loadDesktopLayout();
    QHash<QString, QPoint> placed;

    restoringLayout_ = true;
    fileModel_->clear();

    const QFileInfoList entries = QDir(desktopDirectory_).entryInfoList(
        QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
        QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

    auto *typedProvider = static_cast<DesktopIconProvider *>(iconProvider_);

    for (const QFileInfo &info : entries) {
        QString label = info.fileName();

        if (info.suffix().compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0) {
            const QHash<QString, QString> desktopFields =
                EdenDesktop::fields(info.absoluteFilePath());
            const QString name = EdenDesktop::localizedName(desktopFields);

            if (!name.isEmpty()) {
                label = name;
            } else {
                label = info.completeBaseName();
            }
        }

        auto *item = new QStandardItem(iconProvider_->icon(info), label);
        item->setData(info.absoluteFilePath(), RolePath);
        item->setData(typedProvider->isImage(info), RoleIsImage);
        item->setEditable(false);
        item->setSizeHint(QSize(kDesktopGridW - 8, kDesktopGridH - 8));
        fileModel_->appendRow(item);

        const QModelIndex index = item->index();
        QPoint position = saved.value(info.fileName());

        if (!saved.contains(info.fileName())) {
            position = nextFreeIconPosition(placed);
        }

        placed.insert(info.fileName(), position);

        static_cast<DesktopListView *>(fileView_)->placeItem(position, index);
    }

    restoringLayout_ = false;
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
    return EdenWallpaper::currentPath();
}

void DesktopWindow::applyWallpaper()
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    wallpaper_ = QPixmap(size());
    wallpaper_.fill(QColor("#20252D"));

    const QPixmap source(wallpaperPath());

    if (!source.isNull()) {
        QPainter painter(&wallpaper_);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect widgetGlobal = geometry();

        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen == nullptr) {
                continue;
            }

            const QRect monitor = screen->geometry();
            const QRect overlap = monitor.intersected(widgetGlobal);

            if (overlap.isEmpty()) {
                continue;
            }

            const QPixmap scaled = source.scaled(
                monitor.size(),
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation);
            const int originX = qMax(0, (scaled.width() - monitor.width()) / 2);
            const int originY = qMax(0, (scaled.height() - monitor.height()) / 2);
            const QRect sourceRect(
                originX + (overlap.x() - monitor.x()),
                originY + (overlap.y() - monitor.y()),
                overlap.width(),
                overlap.height());
            const QRect destination = overlap.translated(-widgetGlobal.topLeft());
            painter.drawPixmap(destination, scaled, sourceRect);
        }
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
    const QString path = index.data(RolePath).toString();
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

    if (info.suffix().compare(QLatin1String("deb"), Qt::CaseInsensitive) == 0) {
        const QString installer = QDir(
            QCoreApplication::applicationDirPath()
        ).filePath(QStringLiteral("eden-installer"));

        if (QProcess::startDetached(installer, {info.absoluteFilePath()})) {
            return;
        }

        const QString fallback =
            QStandardPaths::findExecutable(QStringLiteral("eden-installer"));

        if (!fallback.isEmpty() &&
            QProcess::startDetached(fallback, {info.absoluteFilePath()})) {
            return;
        }
    }

    if (info.suffix().compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0) {
        qint64 pid = 0;
        const QHash<QString, QString> desktopFields = EdenDesktop::fields(path);
        const QString name = EdenDesktop::localizedName(desktopFields);

        if (EdenDesktop::launchDesktopFile(path, &pid)) {
            if (DockBar *dock = DockBar::instance()) {
                dock->notifyOpening(
                    name.isEmpty() ? info.completeBaseName() : name,
                    EdenDesktop::iconFor(desktopFields),
                    EdenDesktop::matchHints(desktopFields),
                    pid);
            }

            return;
        }

        qWarning() << "Não foi possível executar o atalho:" << path;
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}



void DesktopWindow::refreshWallpaper()
{
    if (fileView_ != nullptr) {
        fileView_->setGeometry(rect());
    }

    applyWallpaper();
}

void DesktopWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshWallpaper();
}

void DesktopWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    refreshWallpaper();
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

bool DesktopWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (fileView_ != nullptr &&
        watched == fileView_->viewport() &&
        event != nullptr &&
        event->type() == QEvent::MouseButtonRelease &&
        !restoringLayout_) {
        scheduleSaveDesktopLayout();
    }

    return QWidget::eventFilter(watched, event);
}

void DesktopWindow::closeEvent(QCloseEvent *event)
{
    saveDesktopLayout();

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

        const QByteArray sessionId = qgetenv("XDG_SESSION_ID");

        if (!sessionId.isEmpty()) {
            const QString loginctl =
                QStandardPaths::findExecutable(QStringLiteral("loginctl"));

            if (!loginctl.isEmpty()) {
                QProcess::startDetached(
                    loginctl,
                    {QStringLiteral("terminate-session"),
                     QString::fromUtf8(sessionId)});
            }
        }

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
