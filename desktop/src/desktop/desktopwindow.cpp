
#include "desktopwindow.hpp"
#include "desktop/desktopwidgets.hpp"
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
#include <QLineEdit>
#include <QShortcut>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QScreen>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QKeyEvent>
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
        ensureIconTheme();

        const QStyle *style = QApplication::style();
        directoryIcon_ = themeIcon(
            {QStringLiteral("folder"), QStringLiteral("inode-directory")},
            style->standardIcon(QStyle::SP_DirIcon));
        fileIcon_ = themeIcon(
            {QStringLiteral("text-x-generic"), QStringLiteral("text-plain")},
            style->standardIcon(QStyle::SP_FileIcon));
    }

    [[nodiscard]] bool isImage(const QFileInfo &info) const
    {
        if (!info.isFile()) {
            return false;
        }

        const QMimeType mime = mimeDb_.mimeTypeForFile(
            info,
            QMimeDatabase::MatchExtension);
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

        const QString suffix = info.suffix().toLower();

        if (suffix == QLatin1String("desktop")) {
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

        const QString typeKey = suffix.isEmpty()
            ? QStringLiteral("file")
            : suffix;

        if (typeIconCache_.contains(typeKey)) {
            return typeIconCache_.value(typeKey);
        }

        const QMimeType mime = mimeDb_.mimeTypeForFile(
            info,
            QMimeDatabase::MatchExtension);
        const QIcon resolved = themeIcon(iconCandidates(suffix, mime), QIcon());

        if (!resolved.isNull()) {
            typeIconCache_.insert(typeKey, resolved);
            return resolved;
        }

        const QIcon badge = badgeIcon(suffix, mime);
        typeIconCache_.insert(typeKey, badge);
        return badge;
    }

private:
    static void ensureIconTheme()
    {
        if (!QIcon::themeName().isEmpty()) {
            return;
        }

        const QStringList preferred = {
            QStringLiteral("Papirus"),
            QStringLiteral("Yaru"),
            QStringLiteral("Adwaita"),
            QStringLiteral("Humanity"),
            QStringLiteral("hicolor"),
        };

        for (const QString &theme : preferred) {
            if (QDir(QStringLiteral("/usr/share/icons/") + theme).exists()) {
                QIcon::setThemeName(theme);
                return;
            }
        }
    }

    [[nodiscard]] static QStringList iconCandidates(
        const QString &suffix,
        const QMimeType &mime)
    {
        QStringList names;

        // Extension-first: freedesktop MIME often maps .conf and .txt both to text/plain.
        static const QHash<QString, QStringList> byExtension = {
            {QStringLiteral("txt"), {QStringLiteral("text-plain"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("text"), {QStringLiteral("text-plain"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("log"), {QStringLiteral("text-x-log"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("md"), {QStringLiteral("text-markdown"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("markdown"), {QStringLiteral("text-markdown"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("conf"), {QStringLiteral("text-x-generic-template"), QStringLiteral("preferences-system"), QStringLiteral("application-x-wine-extension-ini")}},
            {QStringLiteral("cfg"), {QStringLiteral("text-x-generic-template"), QStringLiteral("preferences-system")}},
            {QStringLiteral("ini"), {QStringLiteral("text-x-generic-template"), QStringLiteral("preferences-system")}},
            {QStringLiteral("toml"), {QStringLiteral("text-x-generic-template"), QStringLiteral("text-x-cmake")}},
            {QStringLiteral("yaml"), {QStringLiteral("text-x-generic-template"), QStringLiteral("application-x-yaml")}},
            {QStringLiteral("yml"), {QStringLiteral("text-x-generic-template"), QStringLiteral("application-x-yaml")}},
            {QStringLiteral("json"), {QStringLiteral("application-json"), QStringLiteral("text-x-script")}},
            {QStringLiteral("xml"), {QStringLiteral("text-xml"), QStringLiteral("application-xml")}},
            {QStringLiteral("csv"), {QStringLiteral("text-csv"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("pdf"), {QStringLiteral("application-pdf"), QStringLiteral("x-office-document")}},
            {QStringLiteral("doc"), {QStringLiteral("x-office-document"), QStringLiteral("application-msword")}},
            {QStringLiteral("docx"), {QStringLiteral("x-office-document"), QStringLiteral("application-vnd.openxmlformats-officedocument.wordprocessingml.document")}},
            {QStringLiteral("xls"), {QStringLiteral("x-office-spreadsheet"), QStringLiteral("application-vnd.ms-excel")}},
            {QStringLiteral("xlsx"), {QStringLiteral("x-office-spreadsheet")}},
            {QStringLiteral("ppt"), {QStringLiteral("x-office-presentation")}},
            {QStringLiteral("pptx"), {QStringLiteral("x-office-presentation")}},
            {QStringLiteral("zip"), {QStringLiteral("application-zip"), QStringLiteral("package-x-generic")}},
            {QStringLiteral("gz"), {QStringLiteral("application-x-gz"), QStringLiteral("package-x-generic")}},
            {QStringLiteral("tar"), {QStringLiteral("application-x-tar"), QStringLiteral("package-x-generic")}},
            {QStringLiteral("7z"), {QStringLiteral("application-x-7z-compressed"), QStringLiteral("package-x-generic")}},
            {QStringLiteral("rar"), {QStringLiteral("application-x-rar"), QStringLiteral("package-x-generic")}},
            {QStringLiteral("deb"), {QStringLiteral("application-vnd.debian.binary-package"), QStringLiteral("package-x-generic")}},
            {QStringLiteral("sh"), {QStringLiteral("application-x-shellscript"), QStringLiteral("text-x-script")}},
            {QStringLiteral("bash"), {QStringLiteral("application-x-shellscript"), QStringLiteral("text-x-script")}},
            {QStringLiteral("py"), {QStringLiteral("text-x-python"), QStringLiteral("text-x-script")}},
            {QStringLiteral("js"), {QStringLiteral("text-javascript"), QStringLiteral("text-x-script")}},
            {QStringLiteral("ts"), {QStringLiteral("text-x-typescript"), QStringLiteral("text-x-script")}},
            {QStringLiteral("c"), {QStringLiteral("text-x-csrc"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("cpp"), {QStringLiteral("text-x-c++src"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("h"), {QStringLiteral("text-x-chdr"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("hpp"), {QStringLiteral("text-x-c++hdr"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("rs"), {QStringLiteral("text-rust"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("go"), {QStringLiteral("text-x-go"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("html"), {QStringLiteral("text-html"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("css"), {QStringLiteral("text-css"), QStringLiteral("text-x-generic")}},
            {QStringLiteral("mp3"), {QStringLiteral("audio-x-generic"), QStringLiteral("audio-mpeg")}},
            {QStringLiteral("wav"), {QStringLiteral("audio-x-generic"), QStringLiteral("audio-x-wav")}},
            {QStringLiteral("mp4"), {QStringLiteral("video-x-generic"), QStringLiteral("video-mp4")}},
            {QStringLiteral("mkv"), {QStringLiteral("video-x-generic"), QStringLiteral("video-x-matroska")}},
            {QStringLiteral("avi"), {QStringLiteral("video-x-generic")}},
        };

        names.append(byExtension.value(suffix));

        if (!mime.iconName().isEmpty()) {
            names.push_back(mime.iconName());
        }

        if (!mime.genericIconName().isEmpty()) {
            names.push_back(mime.genericIconName());
        }

        if (!mime.name().isEmpty()) {
            names.push_back(QString(mime.name()).replace(QLatin1Char('/'), QLatin1Char('-')));
        }

        for (const QString &parent : mime.parentMimeTypes()) {
            names.push_back(QString(parent).replace(QLatin1Char('/'), QLatin1Char('-')));
        }

        names.removeDuplicates();
        return names;
    }

    [[nodiscard]] static QIcon themeIcon(const QStringList &names, const QIcon &fallback)
    {
        for (const QString &name : names) {
            if (name.isEmpty()) {
                continue;
            }

            const QIcon icon = QIcon::fromTheme(name);

            if (!icon.isNull()) {
                return icon;
            }
        }

        return fallback;
    }

    [[nodiscard]] static QColor badgeColor(const QString &suffix)
    {
        if (suffix == QLatin1String("conf") ||
            suffix == QLatin1String("cfg") ||
            suffix == QLatin1String("ini") ||
            suffix == QLatin1String("toml") ||
            suffix == QLatin1String("yaml") ||
            suffix == QLatin1String("yml")) {
            return QColor(0xE0, 0x8A, 0x2E);
        }

        if (suffix == QLatin1String("txt") ||
            suffix == QLatin1String("text") ||
            suffix == QLatin1String("log") ||
            suffix == QLatin1String("md")) {
            return QColor(0x3B, 0x82, 0xF6);
        }

        if (suffix == QLatin1String("pdf")) {
            return QColor(0xDC, 0x26, 0x26);
        }

        if (suffix == QLatin1String("zip") ||
            suffix == QLatin1String("gz") ||
            suffix == QLatin1String("tar") ||
            suffix == QLatin1String("7z") ||
            suffix == QLatin1String("rar") ||
            suffix == QLatin1String("deb")) {
            return QColor(0xCA, 0x8A, 0x04);
        }

        if (suffix == QLatin1String("sh") ||
            suffix == QLatin1String("py") ||
            suffix == QLatin1String("js") ||
            suffix == QLatin1String("ts") ||
            suffix == QLatin1String("c") ||
            suffix == QLatin1String("cpp") ||
            suffix == QLatin1String("h") ||
            suffix == QLatin1String("hpp") ||
            suffix == QLatin1String("rs") ||
            suffix == QLatin1String("go")) {
            return QColor(0x16, 0xA3, 0x4A);
        }

        if (suffix == QLatin1String("mp3") ||
            suffix == QLatin1String("wav") ||
            suffix == QLatin1String("flac")) {
            return QColor(0xA8, 0x55, 0xF7);
        }

        if (suffix == QLatin1String("mp4") ||
            suffix == QLatin1String("mkv") ||
            suffix == QLatin1String("avi")) {
            return QColor(0xDB, 0x27, 0x77);
        }

        return QColor(0x64, 0x74, 0x8B);
    }

    [[nodiscard]] static QIcon badgeIcon(const QString &suffix, const QMimeType &mime)
    {
        Q_UNUSED(mime);

        QPixmap canvas(kDesktopIconSize, kDesktopIconSize);
        canvas.fill(Qt::transparent);

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);

        const QColor accent = badgeColor(suffix);
        const QRectF page(10, 6, 40, 50);
        QPainterPath sheet;
        sheet.moveTo(page.left(), page.top());
        sheet.lineTo(page.right() - 10, page.top());
        sheet.lineTo(page.right(), page.top() + 10);
        sheet.lineTo(page.right(), page.bottom());
        sheet.lineTo(page.left(), page.bottom());
        sheet.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xF8, 0xFA, 0xFC));
        painter.drawPath(sheet);
        painter.setBrush(QColor(0xE2, 0xE8, 0xF0));
        QPolygonF fold;
        fold << QPointF(page.right() - 10, page.top())
             << QPointF(page.right(), page.top() + 10)
             << QPointF(page.right() - 10, page.top() + 10);
        painter.drawPolygon(fold);

        painter.setPen(QPen(QColor(0xCB, 0xD5, 0xE1), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(sheet);

        const QString label = suffix.isEmpty()
            ? QStringLiteral("FILE")
            : suffix.left(4).toUpper();
        const QRectF badge(8, 34, 48, 18);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawRoundedRect(badge, 5, 5);

        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(label.size() >= 4 ? 9 : 10);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, label);
        painter.end();

        return QIcon(canvas);
    }

    QIcon directoryIcon_;
    QIcon fileIcon_;
    mutable QMimeDatabase mimeDb_;
    mutable QHash<QString, QIcon> thumbnailCache_;
    mutable QHash<QString, QIcon> typeIconCache_;
};

class DesktopListView final : public QListView
{
public:
    using QListView::QListView;

    void clearPlacements()
    {
        placements_.clear();
    }

    void placeItem(const QPoint &position, const QModelIndex &index)
    {
        if (!index.isValid()) {
            return;
        }

        const QString key = placementKey(index);

        if (!key.isEmpty()) {
            placements_.insert(key, position);
        }

        setPositionForIndex(position, index);
    }

    void capturePlacementsFromView()
    {
        if (model() == nullptr) {
            return;
        }

        for (int row = 0; row < model()->rowCount(); ++row) {
            const QModelIndex index = model()->index(row, 0);
            const QString key = placementKey(index);

            if (key.isEmpty()) {
                continue;
            }

            placements_.insert(key, visualRect(index).topLeft());
        }
    }

    void reapplyPlacements()
    {
        if (model() == nullptr || placements_.isEmpty()) {
            return;
        }

        for (int row = 0; row < model()->rowCount(); ++row) {
            const QModelIndex index = model()->index(row, 0);
            const QString key = placementKey(index);

            if (key.isEmpty() || !placements_.contains(key)) {
                continue;
            }

            setPositionForIndex(placements_.value(key), index);
        }
    }

protected:
    void doItemsLayout() override
    {
        // QListView's IconMode layout forces a left-to-right grid and wipes
        // free positions after model/resizes. Keep geometries, then restore.
        QListView::doItemsLayout();
        reapplyPlacements();
    }

private:
    [[nodiscard]] static QString placementKey(const QModelIndex &index)
    {
        const QString path = index.data(Qt::UserRole).toString();

        if (path.isEmpty()) {
            return {};
        }

        return QFileInfo(path).fileName();
    }

    QHash<QString, QPoint> placements_;
};

class DesktopItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] static QRect iconRectFor(const QStyleOptionViewItem &option)
    {
        const QSize iconSize = option.decorationSize;
        return {
            option.rect.x() + (option.rect.width() - iconSize.width()) / 2,
            option.rect.y() + 6,
            iconSize.width(),
            iconSize.height()};
    }

    [[nodiscard]] static QRect textRectFor(const QStyleOptionViewItem &option)
    {
        const QRect iconRect = iconRectFor(option);
        return {
            option.rect.x() + 4,
            iconRect.bottom() + 4,
            option.rect.width() - 8,
            std::max(22, option.rect.bottom() - iconRect.bottom() - 4)};
    }

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
            painter->setBrush(QColor(0, 229, 255, 48));
            painter->drawRoundedRect(itemOption.rect.adjusted(2, 2, -2, -2), 10, 10);
        } else if ((itemOption.state & QStyle::State_MouseOver) != 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 28));
            painter->drawRoundedRect(itemOption.rect.adjusted(2, 2, -2, -2), 10, 10);
        }

        const QRect iconRect = iconRectFor(itemOption);
        const bool isImage = index.data(Qt::UserRole + 1).toBool();
        const QPixmap pixmap = itemOption.icon.pixmap(itemOption.decorationSize);

        if (isImage && !pixmap.isNull()) {
            const QPixmap fitted = pixmap.scaled(
                itemOption.decorationSize,
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

        if ((itemOption.state & QStyle::State_Editing) == 0) {
            const QRect textRect = textRectFor(itemOption);
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
        }

        painter->restore();
    }

    QWidget *createEditor(
        QWidget *parent,
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);

        auto *editor = new QLineEdit(parent);
        editor->setAlignment(Qt::AlignCenter);
        editor->setFrame(true);
        editor->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  background: rgba(18, 24, 32, 0.92);"
            "  color: #E7F2F6;"
            "  border: 1px solid #00E5FF;"
            "  border-radius: 6px;"
            "  padding: 2px 4px;"
            "  selection-background-color: #00E5FF;"
            "  selection-color: #121820;"
            "}"));
        return editor;
    }

    void updateEditorGeometry(
        QWidget *editor,
        const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        QStyleOptionViewItem itemOption = option;
        initStyleOption(&itemOption, index);
        const QRect textRect = textRectFor(itemOption);
        editor->setGeometry(textRect.adjusted(-2, -1, 2, 4));
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            line->setText(index.data(Qt::EditRole).toString());
            line->selectAll();
        }
    }

    void setModelData(
        QWidget *editor,
        QAbstractItemModel *model,
        const QModelIndex &index) const override
    {
        if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            model->setData(index, line->text().trimmed(), Qt::EditRole);
        }
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
    widgets_ = new DesktopWidgets(this);
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
    fileView_->setEditTriggers(QAbstractItemView::EditKeyPressed);
    fileView_->setSelectionMode(QAbstractItemView::SingleSelection);
    fileView_->setFocusPolicy(Qt::StrongFocus);
    setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(fileView_);

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
        &QListView::clicked,
        this,
        [this](const QModelIndex &index) {
            if (!index.isValid()) {
                return;
            }

            activateWindow();
            fileView_->setFocus(Qt::MouseFocusReason);
            fileView_->setCurrentIndex(index);
        });

    connect(
        fileView_,
        &QListView::doubleClicked,
        this,
        [this](const QModelIndex &index) {
            openItem(index);
        }
    );

    connect(
        fileModel_,
        &QStandardItemModel::itemChanged,
        this,
        [this](QStandardItem *item) {
            if (restoringLayout_ || item == nullptr) {
                return;
            }

            renameDesktopItem(item);
        });

    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    renameShortcut->setContext(Qt::WindowShortcut);
    connect(renameShortcut, &QShortcut::activated, this, [this]() {
        beginRenameSelected();
    });

    auto *pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    pasteShortcut->setContext(Qt::WindowShortcut);
    connect(pasteShortcut, &QShortcut::activated, this, [this]() {
        pasteClipboardImage();
    });

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

    revealTimer_ = new QTimer(this);
    revealTimer_->setSingleShot(true);
    revealTimer_->setInterval(200);
    connect(revealTimer_, &QTimer::timeout, this, [this]() {
        revealDesktopFiles();
    });

    directoryWatcher_ = new QFileSystemWatcher(this);
    directoryWatcher_->addPath(desktopDirectory_);
    connect(
        directoryWatcher_,
        &QFileSystemWatcher::directoryChanged,
        this,
        [this](const QString &) {
            scheduleRevealDesktopFiles();
        }
    );

    revealDesktopFiles();

    fileView_->installEventFilter(this);
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

void DesktopWindow::scheduleRevealDesktopFiles()
{
    if (revealTimer_ != nullptr) {
        revealTimer_->start();
    }
}

void DesktopWindow::applyIconPositions()
{
    if (fileView_ == nullptr) {
        return;
    }

    static_cast<DesktopListView *>(fileView_)->reapplyPlacements();
    fileView_->viewport()->update();
}

void DesktopWindow::beginRenameSelected()
{
    if (fileView_ == nullptr || fileModel_ == nullptr) {
        return;
    }

    QModelIndex index = fileView_->currentIndex();

    if (!index.isValid() && !fileView_->selectionModel()->selectedIndexes().isEmpty()) {
        index = fileView_->selectionModel()->selectedIndexes().constFirst();
        fileView_->setCurrentIndex(index);
    }

    if (!index.isValid()) {
        return;
    }

    activateWindow();
    fileView_->setFocus(Qt::ShortcutFocusReason);
    fileView_->setCurrentIndex(index);

    // Force edit regardless of trigger flags.
    QMetaObject::invokeMethod(
        fileView_,
        [this, index]() {
            fileView_->edit(index);
        },
        Qt::QueuedConnection);
}

namespace {

bool updateDesktopEntryName(const QString &path, const QString &name)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    static const QRegularExpression nameLine(
        QStringLiteral("(?m)^Name\\s*=.*$"));

    if (nameLine.match(content).hasMatch()) {
        content.replace(nameLine, QStringLiteral("Name=%1").arg(name));
    } else {
        const int desktopEntry = content.indexOf(QStringLiteral("[Desktop Entry]"));

        if (desktopEntry < 0) {
            return false;
        }

        const int insertAt = content.indexOf(QLatin1Char('\n'), desktopEntry);

        if (insertAt < 0) {
            content += QStringLiteral("\nName=%1\n").arg(name);
        } else {
            content.insert(insertAt + 1, QStringLiteral("Name=%1\n").arg(name));
        }
    }

    QSaveFile out(path);

    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    out.write(content.toUtf8());
    return out.commit();
}

}  // namespace

void DesktopWindow::renameDesktopItem(QStandardItem *item)
{
    if (item == nullptr || fileView_ == nullptr) {
        return;
    }

    const QString path = item->data(RolePath).toString();
    const QString newLabel = item->text().trimmed();
    const QFileInfo info(path);

    if (path.isEmpty() || !info.exists() || newLabel.isEmpty()) {
        restoringLayout_ = true;
        item->setText(info.fileName());
        restoringLayout_ = false;
        return;
    }

    if (info.suffix().compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0) {
        const QHash<QString, QString> fields = EdenDesktop::fields(path);
        const QString currentName = EdenDesktop::localizedName(fields);

        if (newLabel == currentName || newLabel == info.completeBaseName()) {
            return;
        }

        if (!updateDesktopEntryName(path, newLabel)) {
            restoringLayout_ = true;
            item->setText(currentName.isEmpty() ? info.completeBaseName() : currentName);
            restoringLayout_ = false;
        }

        return;
    }

    if (newLabel == info.fileName()) {
        return;
    }

    const QString target = info.dir().filePath(newLabel);

    if (QFileInfo::exists(target)) {
        restoringLayout_ = true;
        item->setText(info.fileName());
        restoringLayout_ = false;
        return;
    }

    // Keep icon position under the new file name.
    QHash<QString, QPoint> layout = loadDesktopLayout();
    const QPoint position = layout.value(
        info.fileName(),
        fileView_->visualRect(item->index()).topLeft());
    layout.remove(info.fileName());
    layout.insert(newLabel, position);

    if (!QFile::rename(path, target)) {
        restoringLayout_ = true;
        item->setText(info.fileName());
        restoringLayout_ = false;
        return;
    }

    restoringLayout_ = true;
    item->setData(target, RolePath);
    item->setText(newLabel);
    restoringLayout_ = false;

    QSaveFile file(layoutConfigPath());

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "# Eden desktop icon positions\n";

        for (auto it = layout.cbegin(); it != layout.cend(); ++it) {
            stream << it.key() << '=' << it.value().x() << ',' << it.value().y()
                   << '\n';
        }

        file.commit();
    }
}

void DesktopWindow::saveDesktopLayout()
{
    if (fileModel_ == nullptr || fileView_ == nullptr || restoringLayout_) {
        return;
    }

    auto *desktopView = static_cast<DesktopListView *>(fileView_);
    desktopView->capturePlacementsFromView();

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

    // Never save here — a mid-refresh visualRect dump would overwrite free
    // positions with QListView's temporary horizontal grid.
    const QHash<QString, QPoint> saved = loadDesktopLayout();
    QHash<QString, QPoint> placed;
    auto *desktopView = static_cast<DesktopListView *>(fileView_);

    restoringLayout_ = true;
    desktopView->clearPlacements();
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

        QPoint position = saved.value(info.fileName());

        if (!saved.contains(info.fileName())) {
            position = nextFreeIconPosition(placed);
        }

        placed.insert(info.fileName(), position);

        auto *item = new QStandardItem(iconProvider_->icon(info), label);
        item->setData(info.absoluteFilePath(), RolePath);
        item->setData(typedProvider->isImage(info), RoleIsImage);
        item->setData(position.x(), RolePosX);
        item->setData(position.y(), RolePosY);
        item->setEditable(true);
        item->setSizeHint(QSize(kDesktopGridW - 8, kDesktopGridH - 8));
        fileModel_->appendRow(item);

        desktopView->placeItem(position, item->index());
    }

    restoringLayout_ = false;
    applyIconPositions();
    QTimer::singleShot(0, this, [this]() {
        applyIconPositions();
    });
    QTimer::singleShot(50, this, [this]() {
        applyIconPositions();
    });
}

void DesktopWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if (fileModel_ == nullptr || fileModel_->rowCount() == 0) {
        revealDesktopFiles();
    } else {
        applyIconPositions();
    }

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

    if (widgets_ != nullptr) {
        widgets_->reposition();
    }

    applyWallpaper();
}

void DesktopWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshWallpaper();
    applyIconPositions();
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
        painter.fillRect(rect(), QColor("#121820"));
        return;
    }

    painter.drawPixmap(0, 0, wallpaper_);
}


bool DesktopWindow::sessionExitAllowed_ = false;

void DesktopWindow::allowSessionExit()
{
    sessionExitAllowed_ = true;
}

void DesktopWindow::keyPressEvent(QKeyEvent *event)
{
    if (event != nullptr && event->key() == Qt::Key_F2) {
        beginRenameSelected();
        event->accept();
        return;
    }

    if (event != nullptr &&
        event->matches(QKeySequence::Paste) &&
        pasteClipboardImage()) {
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

bool DesktopWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (fileView_ != nullptr && event != nullptr) {
        if (watched == fileView_->viewport() &&
            event->type() == QEvent::MouseButtonRelease &&
            !restoringLayout_) {
            scheduleSaveDesktopLayout();
        }

        if ((watched == fileView_ || watched == fileView_->viewport()) &&
            event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);

            if (keyEvent->key() == Qt::Key_F2) {
                beginRenameSelected();
                return true;
            }

            if (keyEvent->matches(QKeySequence::Paste) && pasteClipboardImage()) {
                return true;
            }
        }
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

bool DesktopWindow::pasteClipboardImage()
{
    QClipboard *clipboard = QGuiApplication::clipboard();

    if (clipboard == nullptr) {
        return false;
    }

    const QMimeData *mime = clipboard->mimeData();
    QImage image;

    if (mime != nullptr && mime->hasImage()) {
        image = qvariant_cast<QImage>(mime->imageData());
    }

    if (image.isNull()) {
        const QPixmap pixmap = clipboard->pixmap();

        if (!pixmap.isNull()) {
            image = pixmap.toImage();
        }
    }

    if (image.isNull()) {
        return false;
    }

    QDir().mkpath(desktopDirectory_);

    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss"));
    QString fileName = QStringLiteral("Captura-%1.png").arg(stamp);
    QString path = QDir(desktopDirectory_).filePath(fileName);

    for (int suffix = 2; QFileInfo::exists(path); ++suffix) {
        fileName = QStringLiteral("Captura-%1-%2.png").arg(stamp).arg(suffix);
        path = QDir(desktopDirectory_).filePath(fileName);
    }

    if (!image.save(path, "PNG")) {
        return false;
    }

    clipboard->clear(QClipboard::Clipboard);
    clipboard->clear(QClipboard::Selection);
    return true;
}
