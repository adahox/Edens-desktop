#include "desktop/settingswindow.hpp"
#include "desktop/dockbar.hpp"
#include "desktop/docksettings.hpp"
#include "desktop/monitorlayout.hpp"
#include "desktop/shelllayout.hpp"
#include "desktop/wallpapersettings.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr QColor kBg(18, 22, 32);
constexpr QColor kPanel(14, 18, 26);
constexpr QColor kCard(28, 34, 48);
constexpr QColor kBorder(58, 69, 92);
constexpr QColor kText(242, 244, 248);
constexpr QColor kMuted(168, 176, 194);
constexpr QColor kAccent(0, 122, 255);

}  // namespace

class SettingsWindow::MonitorPreview final : public QWidget
{
public:
    explicit MonitorPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(220);
    }

    void setMonitors(
        const QVector<SettingsWindow::MonitorInfo> &monitors,
        const QString &primary,
        const QString &secondary)
    {
        monitors_ = monitors;
        primary_ = primary;
        secondary_ = secondary;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(22, 26, 36));

        QVector<SettingsWindow::MonitorInfo> connected;
        for (const auto &monitor : monitors_) {
            if (monitor.connected) {
                connected.push_back(monitor);
            }
        }

        if (connected.isEmpty()) {
            painter.setPen(kMuted);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Nenhum monitor conectado"));
            return;
        }

        int minX = connected.first().x;
        int minY = connected.first().y;
        int maxX = connected.first().x + connected.first().width;
        int maxY = connected.first().y + connected.first().height;

        for (const auto &monitor : connected) {
            minX = std::min(minX, monitor.x);
            minY = std::min(minY, monitor.y);
            maxX = std::max(maxX, monitor.x + monitor.width);
            maxY = std::max(maxY, monitor.y + monitor.height);
        }

        const int worldW = std::max(1, maxX - minX);
        const int worldH = std::max(1, maxY - minY);
        const QRect area = rect().adjusted(28, 24, -28, -24);
        const double scale = std::min(
            static_cast<double>(area.width()) / worldW,
            static_cast<double>(area.height()) / worldH) * 0.88;
        const double drawW = worldW * scale;
        const double drawH = worldH * scale;
        const double originX = area.center().x() - drawW / 2.0;
        const double originY = area.center().y() - drawH / 2.0;

        for (const auto &monitor : connected) {
            const QRectF box(
                originX + (monitor.x - minX) * scale,
                originY + (monitor.y - minY) * scale,
                monitor.width * scale,
                monitor.height * scale);

            const bool isPrimary = monitor.name == primary_;
            const bool isSecondary = monitor.name == secondary_;

            QPainterPath path;
            path.addRoundedRect(box, 10, 10);
            painter.fillPath(path, isPrimary ? QColor(0, 122, 255, 55) : QColor(40, 48, 64));
            painter.setPen(QPen(
                isPrimary ? kAccent : (isSecondary ? QColor(80, 200, 140) : kBorder),
                isPrimary || isSecondary ? 2.4 : 1.4));
            painter.drawPath(path);

            painter.setPen(kText);
            QFont title = font();
            title.setPointSize(11);
            title.setWeight(QFont::DemiBold);
            painter.setFont(title);
            painter.drawText(
                box.adjusted(10, 10, -10, -10),
                Qt::AlignTop | Qt::AlignHCenter,
                monitor.name);

            painter.setPen(kMuted);
            QFont detail = font();
            detail.setPointSize(10);
            painter.setFont(detail);
            const QString role = isPrimary
                ? QStringLiteral("Principal")
                : (isSecondary ? QStringLiteral("Secundário") : QStringLiteral("Conectado"));
            painter.drawText(
                box.adjusted(10, 34, -10, -10),
                Qt::AlignTop | Qt::AlignHCenter,
                QStringLiteral("%1\n%2×%3")
                    .arg(role)
                    .arg(monitor.width)
                    .arg(monitor.height));
        }
    }

private:
    QVector<SettingsWindow::MonitorInfo> monitors_;
    QString primary_;
    QString secondary_;
};

SettingsWindow *SettingsWindow::active_ = nullptr;

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent)
{
    active_ = this;
    setWindowTitle(QStringLiteral("Ajustes do Sistema"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    resize(kWidth, kHeight);
    setStyleSheet(QStringLiteral(
        "SettingsWindow { background: #121620; color: #F2F4F8; }"
        "QLabel { background: transparent; color: #F2F4F8; }"
        "QListWidget {"
        "  background: #0E121A; border: none; color: #A8B0C2; outline: none; padding: 10px;"
        "}"
        "QListWidget::item {"
        "  padding: 12px 14px; border-radius: 10px; margin: 2px 0;"
        "}"
        "QListWidget::item:selected {"
        "  background: rgba(0,122,255,0.22); color: #F2F4F8; font-weight: 600;"
        "}"
        "QComboBox {"
        "  background: #1C2230; color: #F2F4F8; border: 1px solid #3A455C;"
        "  border-radius: 10px; padding: 8px 12px; min-width: 220px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: #1C2230; color: #F2F4F8; selection-background-color: #007AFF;"
        "}"
        "QPushButton {"
        "  background: #24304A; color: #F2F4F8; border: 1px solid #3A455C;"
        "  border-radius: 10px; padding: 9px 16px;"
        "}"
        "QPushButton:hover { background: #314263; }"
        "QPushButton#applyButton {"
        "  background: #007AFF; border: none; font-weight: 600;"
        "}"
        "QPushButton#applyButton:hover { background: #0A84FF; }"
        "QSlider { min-height: 22px; }"));

    buildUi();
}

void SettingsWindow::open()
{
    if (active_ == nullptr) {
        active_ = new SettingsWindow;
    }

    active_->refreshMonitors();
    active_->loadDockControls();
    active_->loadWallpaperControls();
    active_->show();
    active_->raise();
    active_->activateWindow();

    if (DockBar *dock = DockBar::instance()) {
        dock->notifyOpening(
            QStringLiteral("Ajustes"),
            active_->style()->standardIcon(QStyle::SP_FileDialogDetailedView),
            {},
            0,
            {QStringLiteral("Ajustes do Sistema")});
    }
}

SettingsWindow *SettingsWindow::instance()
{
    return active_;
}

void SettingsWindow::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    nav_ = new QListWidget(this);
    nav_->setFixedWidth(210);
    nav_->addItem(QStringLiteral("Monitores"));
    nav_->addItem(QStringLiteral("Dock"));
    nav_->addItem(QStringLiteral("Papel de parede"));
    nav_->setCurrentRow(0);
    connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
        setSection(row);
    });
    root->addWidget(nav_);

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 20, 24, 20);
    contentLayout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("Ajustes do Sistema"), content);
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setWeight(QFont::Bold);
    title->setFont(titleFont);
    contentLayout->addWidget(title);

    subtitle_ = new QLabel(
        QStringLiteral("Configure o monitor principal e o secundário."),
        content);
    subtitle_->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    contentLayout->addWidget(subtitle_);

    stack_ = new QStackedWidget(content);
    stack_->addWidget(buildMonitorsPage());
    stack_->addWidget(buildDockPage());
    stack_->addWidget(buildWallpaperPage());
    contentLayout->addWidget(stack_, 1);
    root->addWidget(content, 1);

    loadDockControls();
    loadWallpaperControls();
}

QWidget *SettingsWindow::buildMonitorsPage()
{
    auto *page = new QWidget(stack_);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *card = new QFrame(page);
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: #1C2230; border: 1px solid #3A455C; border-radius: 16px; }"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(12);

    auto *heading = new QLabel(QStringLiteral("Disposição dos monitores"), card);
    QFont headingFont = heading->font();
    headingFont.setPointSize(13);
    headingFont.setWeight(QFont::DemiBold);
    heading->setFont(headingFont);
    cardLayout->addWidget(heading);

    preview_ = new SettingsWindow::MonitorPreview(card);
    cardLayout->addWidget(preview_, 1);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);

    primaryCombo_ = new QComboBox(card);
    secondaryCombo_ = new QComboBox(card);
    secondarySideCombo_ = new QComboBox(card);
    secondarySideCombo_->addItem(QStringLiteral("À direita do principal"), QStringLiteral("right"));
    secondarySideCombo_->addItem(QStringLiteral("À esquerda do principal"), QStringLiteral("left"));
    secondarySideCombo_->addItem(QStringLiteral("Acima do principal"), QStringLiteral("above"));
    secondarySideCombo_->addItem(QStringLiteral("Abaixo do principal"), QStringLiteral("below"));

    form->addRow(QStringLiteral("Monitor padrão"), primaryCombo_);
    form->addRow(QStringLiteral("Monitor secundário"), secondaryCombo_);
    form->addRow(QStringLiteral("Posição do secundário"), secondarySideCombo_);
    cardLayout->addLayout(form);

    connect(primaryCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (primaryCombo_->currentData().toString() == secondaryCombo_->currentData().toString() &&
            secondaryCombo_->count() > 1) {
            const int next = (secondaryCombo_->currentIndex() + 1) % secondaryCombo_->count();
            secondaryCombo_->setCurrentIndex(next);
        }

        updateMonitorPreview();
    });
    connect(secondaryCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateMonitorPreview();
    });

    layout->addWidget(card, 1);

    auto *actions = new QHBoxLayout;
    statusLabel_ = new QLabel(page);
    statusLabel_->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    actions->addWidget(statusLabel_, 1);

    refreshButton_ = new QPushButton(QStringLiteral("Atualizar"), page);
    applyButton_ = new QPushButton(QStringLiteral("Aplicar"), page);
    applyButton_->setObjectName(QStringLiteral("applyButton"));
    actions->addWidget(refreshButton_);
    actions->addWidget(applyButton_);
    layout->addLayout(actions);

    connect(refreshButton_, &QPushButton::clicked, this, [this]() {
        refreshMonitors();
    });
    connect(applyButton_, &QPushButton::clicked, this, [this]() {
        applyMonitorLayout();
    });

    return page;
}

void SettingsWindow::setSection(int section)
{
    const int index = std::max(0, section);
    stack_->setCurrentIndex(index);

    if (subtitle_ == nullptr) {
        return;
    }

    if (index == 1) {
        subtitle_->setText(QStringLiteral("Ajuste tamanho, fonte e espaçamento do Dock."));
        loadDockControls();
    } else if (index == 2) {
        subtitle_->setText(QStringLiteral("Escolha um papel de parede do Eden ou um arquivo seu."));
        loadWallpaperControls();
    } else {
        subtitle_->setText(QStringLiteral("Configure o monitor principal e o secundário."));
    }
}

namespace {

QWidget *makeSliderRow(
    QWidget *parent,
    const QString &label,
    QSlider **sliderOut,
    QLabel **valueOut,
    int minimum,
    int maximum,
    int value)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *name = new QLabel(label, row);
    name->setMinimumWidth(180);
    name->setStyleSheet(QStringLiteral("color: #F2F4F8;"));
    layout->addWidget(name);

    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    slider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal {"
        "  height: 6px; background: #2A3346; border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 16px; margin: -6px 0; border-radius: 8px;"
        "  background: #007AFF;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #007AFF; border-radius: 3px;"
        "}"));
    layout->addWidget(slider, 1);

    auto *valueLabel = new QLabel(QString::number(value), row);
    valueLabel->setMinimumWidth(36);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    layout->addWidget(valueLabel);

    *sliderOut = slider;
    *valueOut = valueLabel;
    return row;
}

}  // namespace

QWidget *SettingsWindow::buildDockPage()
{
    auto *page = new QWidget(stack_);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *card = new QFrame(page);
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: #1C2230; border: 1px solid #3A455C; border-radius: 16px; }"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(14);

    auto *heading = new QLabel(QStringLiteral("Aparência do Dock"), card);
    QFont headingFont = heading->font();
    headingFont.setPointSize(13);
    headingFont.setWeight(QFont::DemiBold);
    heading->setFont(headingFont);
    cardLayout->addWidget(heading);

    auto *hint = new QLabel(
        QStringLiteral("As alterações aparecem na hora. Use Aplicar para salvar."),
        card);
    hint->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    cardLayout->addWidget(hint);

    const EdenDock::Settings defaults = EdenDock::defaults();
    cardLayout->addWidget(makeSliderRow(
        card,
        QStringLiteral("Tamanho dos ícones"),
        &dockIconSizeSlider_,
        &dockIconSizeValue_,
        32,
        80,
        defaults.iconSize));
    cardLayout->addWidget(makeSliderRow(
        card,
        QStringLiteral("Tamanho ao passar o mouse"),
        &dockHoverSizeSlider_,
        &dockHoverSizeValue_,
        40,
        112,
        defaults.iconHoverSize));
    cardLayout->addWidget(makeSliderRow(
        card,
        QStringLiteral("Tamanho da fonte"),
        &dockFontSizeSlider_,
        &dockFontSizeValue_,
        9,
        20,
        defaults.fontSize));
    cardLayout->addWidget(makeSliderRow(
        card,
        QStringLiteral("Espaço entre ícones"),
        &dockGapSlider_,
        &dockGapValue_,
        2,
        20,
        defaults.iconGap));
    cardLayout->addWidget(makeSliderRow(
        card,
        QStringLiteral("Distância da borda inferior"),
        &dockMarginSlider_,
        &dockMarginValue_,
        0,
        40,
        defaults.bottomMargin));

    cardLayout->addStretch(1);
    layout->addWidget(card, 1);

    auto *actions = new QHBoxLayout;
    dockStatusLabel_ = new QLabel(page);
    dockStatusLabel_->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    actions->addWidget(dockStatusLabel_, 1);

    dockResetButton_ = new QPushButton(QStringLiteral("Restaurar"), page);
    dockApplyButton_ = new QPushButton(QStringLiteral("Aplicar"), page);
    dockApplyButton_->setObjectName(QStringLiteral("applyButton"));
    actions->addWidget(dockResetButton_);
    actions->addWidget(dockApplyButton_);
    layout->addLayout(actions);

    const auto syncLabels = [this]() {
        if (dockIconSizeValue_ != nullptr) {
            dockIconSizeValue_->setText(QString::number(dockIconSizeSlider_->value()));
        }
        if (dockHoverSizeValue_ != nullptr) {
            dockHoverSizeValue_->setText(QString::number(dockHoverSizeSlider_->value()));
        }
        if (dockFontSizeValue_ != nullptr) {
            dockFontSizeValue_->setText(QString::number(dockFontSizeSlider_->value()));
        }
        if (dockGapValue_ != nullptr) {
            dockGapValue_->setText(QString::number(dockGapSlider_->value()));
        }
        if (dockMarginValue_ != nullptr) {
            dockMarginValue_->setText(QString::number(dockMarginSlider_->value()));
        }
    };

    const auto onChanged = [this, syncLabels](int) {
        if (dockHoverSizeSlider_->value() < dockIconSizeSlider_->value() + 4) {
            dockHoverSizeSlider_->blockSignals(true);
            dockHoverSizeSlider_->setValue(dockIconSizeSlider_->value() + 4);
            dockHoverSizeSlider_->blockSignals(false);
        }

        syncLabels();
        previewDockSettings();
    };

    connect(dockIconSizeSlider_, &QSlider::valueChanged, this, onChanged);
    connect(dockHoverSizeSlider_, &QSlider::valueChanged, this, onChanged);
    connect(dockFontSizeSlider_, &QSlider::valueChanged, this, onChanged);
    connect(dockGapSlider_, &QSlider::valueChanged, this, onChanged);
    connect(dockMarginSlider_, &QSlider::valueChanged, this, onChanged);
    connect(dockApplyButton_, &QPushButton::clicked, this, [this]() {
        applyDockSettings();
    });
    connect(dockResetButton_, &QPushButton::clicked, this, [this]() {
        const EdenDock::Settings defaults = EdenDock::defaults();
        dockIconSizeSlider_->setValue(defaults.iconSize);
        dockHoverSizeSlider_->setValue(defaults.iconHoverSize);
        dockFontSizeSlider_->setValue(defaults.fontSize);
        dockGapSlider_->setValue(defaults.iconGap);
        dockMarginSlider_->setValue(defaults.bottomMargin);
        applyDockSettings();
        dockStatusLabel_->setText(QStringLiteral("Padrões restaurados e salvos."));
    });

    return page;
}

void SettingsWindow::loadDockControls()
{
    if (dockIconSizeSlider_ == nullptr) {
        return;
    }

    EdenDock::Settings settings = EdenDock::load();

    if (DockBar *dock = DockBar::instance()) {
        settings = dock->settings();
    }

    const QSignalBlocker blockers[] = {
        QSignalBlocker(dockIconSizeSlider_),
        QSignalBlocker(dockHoverSizeSlider_),
        QSignalBlocker(dockFontSizeSlider_),
        QSignalBlocker(dockGapSlider_),
        QSignalBlocker(dockMarginSlider_)
    };
    (void)blockers;

    dockIconSizeSlider_->setValue(settings.iconSize);
    dockHoverSizeSlider_->setValue(settings.iconHoverSize);
    dockFontSizeSlider_->setValue(settings.fontSize);
    dockGapSlider_->setValue(settings.iconGap);
    dockMarginSlider_->setValue(settings.bottomMargin);

    dockIconSizeValue_->setText(QString::number(settings.iconSize));
    dockHoverSizeValue_->setText(QString::number(settings.iconHoverSize));
    dockFontSizeValue_->setText(QString::number(settings.fontSize));
    dockGapValue_->setText(QString::number(settings.iconGap));
    dockMarginValue_->setText(QString::number(settings.bottomMargin));
}

void SettingsWindow::previewDockSettings()
{
    EdenDock::Settings settings;
    settings.iconSize = dockIconSizeSlider_->value();
    settings.iconHoverSize = dockHoverSizeSlider_->value();
    settings.fontSize = dockFontSizeSlider_->value();
    settings.iconGap = dockGapSlider_->value();
    settings.bottomMargin = dockMarginSlider_->value();
    EdenDock::apply(settings);

    if (dockStatusLabel_ != nullptr) {
        dockStatusLabel_->setText(QStringLiteral("Pré-visualização ativa. Clique em Aplicar para salvar."));
    }
}

void SettingsWindow::applyDockSettings()
{
    EdenDock::Settings settings;
    settings.iconSize = dockIconSizeSlider_->value();
    settings.iconHoverSize = dockHoverSizeSlider_->value();
    settings.fontSize = dockFontSizeSlider_->value();
    settings.iconGap = dockGapSlider_->value();
    settings.bottomMargin = dockMarginSlider_->value();
    settings.clamp();

    EdenDock::apply(settings);

    if (!EdenDock::save(settings)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Dock"),
            QStringLiteral("Não foi possível salvar a configuração do Dock."));
        return;
    }

    if (dockStatusLabel_ != nullptr) {
        dockStatusLabel_->setText(QStringLiteral("Configuração do Dock salva."));
    }
}

QWidget *SettingsWindow::buildWallpaperPage()
{
    auto *page = new QWidget(stack_);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *card = new QFrame(page);
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: #1C2230; border: 1px solid #3A455C; border-radius: 16px; }"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(14);

    auto *heading = new QLabel(QStringLiteral("Papel de parede"), card);
    QFont headingFont = heading->font();
    headingFont.setPointSize(13);
    headingFont.setWeight(QFont::DemiBold);
    heading->setFont(headingFont);
    cardLayout->addWidget(heading);

    wallpaperPreview_ = new QLabel(card);
    wallpaperPreview_->setMinimumHeight(180);
    wallpaperPreview_->setAlignment(Qt::AlignCenter);
    wallpaperPreview_->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background: #0E121A;"
        "  border: 1px solid #3A455C;"
        "  border-radius: 14px;"
        "  color: #A8B0C2;"
        "}"));
    wallpaperPreview_->setScaledContents(false);
    cardLayout->addWidget(wallpaperPreview_);

    wallpaperPathLabel_ = new QLabel(card);
    wallpaperPathLabel_->setStyleSheet(QStringLiteral("color: #A8B0C2; font-size: 12px;"));
    wallpaperPathLabel_->setWordWrap(true);
    cardLayout->addWidget(wallpaperPathLabel_);

    auto *galleryLabel = new QLabel(QStringLiteral("Biblioteca do Eden"), card);
    galleryLabel->setStyleSheet(QStringLiteral("color: #F2F4F8; font-weight: 600;"));
    cardLayout->addWidget(galleryLabel);

    wallpaperGallery_ = new QListWidget(card);
    wallpaperGallery_->setViewMode(QListView::IconMode);
    wallpaperGallery_->setIconSize(QSize(140, 84));
    wallpaperGallery_->setResizeMode(QListWidget::Adjust);
    wallpaperGallery_->setMovement(QListWidget::Static);
    wallpaperGallery_->setSpacing(10);
    wallpaperGallery_->setMinimumHeight(150);
    wallpaperGallery_->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background: #0E121A; border: 1px solid #3A455C; border-radius: 12px;"
        "  padding: 10px;"
        "}"
        "QListWidget::item {"
        "  background: #1C2230; border: 1px solid #3A455C; border-radius: 10px;"
        "  padding: 6px; margin: 2px;"
        "}"
        "QListWidget::item:selected {"
        "  background: rgba(0,122,255,0.22); border: 1px solid #007AFF;"
        "}"));
    cardLayout->addWidget(wallpaperGallery_, 1);

    connect(wallpaperGallery_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item == nullptr) {
            return;
        }

        selectWallpaperPath(item->data(Qt::UserRole).toString());
    });

    layout->addWidget(card, 1);

    auto *actions = new QHBoxLayout;
    wallpaperStatusLabel_ = new QLabel(page);
    wallpaperStatusLabel_->setStyleSheet(QStringLiteral("color: #A8B0C2;"));
    actions->addWidget(wallpaperStatusLabel_, 1);

    wallpaperChooseButton_ = new QPushButton(QStringLiteral("Escolher arquivo…"), page);
    wallpaperResetButton_ = new QPushButton(QStringLiteral("Padrão"), page);
    wallpaperApplyButton_ = new QPushButton(QStringLiteral("Aplicar"), page);
    wallpaperApplyButton_->setObjectName(QStringLiteral("applyButton"));
    actions->addWidget(wallpaperChooseButton_);
    actions->addWidget(wallpaperResetButton_);
    actions->addWidget(wallpaperApplyButton_);
    layout->addLayout(actions);

    connect(wallpaperChooseButton_, &QPushButton::clicked, this, [this]() {
        chooseWallpaperFile();
    });
    connect(wallpaperResetButton_, &QPushButton::clicked, this, [this]() {
        resetWallpaperSettings();
    });
    connect(wallpaperApplyButton_, &QPushButton::clicked, this, [this]() {
        applyWallpaperSettings();
    });

    return page;
}

void SettingsWindow::selectWallpaperPath(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }

    wallpaperPendingPath_ = QFileInfo(path).absoluteFilePath();

    if (wallpaperPathLabel_ != nullptr) {
        wallpaperPathLabel_->setText(wallpaperPendingPath_);
    }

    if (wallpaperPreview_ != nullptr) {
        const QPixmap source(wallpaperPendingPath_);

        if (source.isNull()) {
            wallpaperPreview_->setText(QStringLiteral("Não foi possível carregar a imagem"));
            wallpaperPreview_->setPixmap(QPixmap());
        } else {
            wallpaperPreview_->setText({});
            const QSize target = wallpaperPreview_->size().isValid()
                ? wallpaperPreview_->size().boundedTo(QSize(860, 220))
                : QSize(640, 180);
            wallpaperPreview_->setPixmap(source.scaled(
                target.width() > 0 ? target : QSize(640, 180),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
        }
    }

    if (wallpaperGallery_ != nullptr) {
        const QSignalBlocker blocker(wallpaperGallery_);

        for (int index = 0; index < wallpaperGallery_->count(); ++index) {
            QListWidgetItem *item = wallpaperGallery_->item(index);

            if (item != nullptr &&
                item->data(Qt::UserRole).toString() == wallpaperPendingPath_) {
                wallpaperGallery_->setCurrentItem(item);
                break;
            }
        }
    }

    if (wallpaperStatusLabel_ != nullptr) {
        wallpaperStatusLabel_->setText(QStringLiteral("Pré-visualização pronta. Clique em Aplicar."));
    }
}

void SettingsWindow::loadWallpaperControls()
{
    if (wallpaperGallery_ == nullptr) {
        return;
    }

    wallpaperGallery_->clear();
    const QStringList bundled = EdenWallpaper::bundledPaths();

    for (const QString &path : bundled) {
        QPixmap thumb(path);

        if (thumb.isNull()) {
            continue;
        }

        auto *item = new QListWidgetItem(
            QIcon(thumb.scaled(140, 84, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)),
            QFileInfo(path).completeBaseName(),
            wallpaperGallery_);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(156, 110));
    }

    selectWallpaperPath(EdenWallpaper::currentPath());

    if (wallpaperStatusLabel_ != nullptr) {
        wallpaperStatusLabel_->setText(QStringLiteral("Papel de parede atual carregado."));
    }
}

void SettingsWindow::chooseWallpaperFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Escolher papel de parede"),
        QDir::homePath(),
        QStringLiteral("Imagens (*.jpg *.jpeg *.png *.webp *.bmp)"));

    if (path.isEmpty()) {
        return;
    }

    selectWallpaperPath(path);
}

void SettingsWindow::applyWallpaperSettings()
{
    if (wallpaperPendingPath_.isEmpty()) {
        wallpaperPendingPath_ = EdenWallpaper::currentPath();
    }

    if (!EdenWallpaper::apply(wallpaperPendingPath_)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Papel de parede"),
            QStringLiteral("Não foi possível aplicar o papel de parede."));
        return;
    }

    if (wallpaperStatusLabel_ != nullptr) {
        wallpaperStatusLabel_->setText(QStringLiteral("Papel de parede aplicado."));
    }
}

void SettingsWindow::resetWallpaperSettings()
{
    const QString path = EdenWallpaper::defaultPath();

    if (path.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Papel de parede"),
            QStringLiteral("Não encontrei o papel de parede padrão."));
        return;
    }

    selectWallpaperPath(path);
    applyWallpaperSettings();
}

QVector<SettingsWindow::MonitorInfo> SettingsWindow::queryMonitors() const
{
    QVector<MonitorInfo> monitors;
    QProcess process;
    process.start(QStringLiteral("xrandr"), {QStringLiteral("--query")});

    if (!process.waitForFinished(4000)) {
        return monitors;
    }

    const QRegularExpression lineRegex(
        QStringLiteral("^(\\S+)\\s+(connected|disconnected)(\\s+primary)?(?:\\s+(\\d+)x(\\d+)\\+(\\d+)\\+(\\d+))?"));

    const QString output = QString::fromUtf8(process.readAllStandardOutput());

    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = lineRegex.match(line);

        if (!match.hasMatch()) {
            continue;
        }

        MonitorInfo info;
        info.name = match.captured(1);
        info.connected = match.captured(2) == QLatin1String("connected");
        info.primary = !match.captured(3).isEmpty();
        info.width = match.captured(4).toInt();
        info.height = match.captured(5).toInt();
        info.x = match.captured(6).toInt();
        info.y = match.captured(7).toInt();

        if (info.connected && info.width > 0 && info.height > 0) {
            info.mode = QStringLiteral("%1x%2").arg(info.width).arg(info.height);
        }

        monitors.push_back(info);
    }

    return monitors;
}

void SettingsWindow::refreshMonitors()
{
    monitors_ = queryMonitors();

    EdenMonitors::Layout saved;
    const bool hasSaved = EdenMonitors::load(&saved);

    QString previousPrimary = primaryCombo_ != nullptr ? primaryCombo_->currentData().toString() : QString();
    QString previousSecondary =
        secondaryCombo_ != nullptr ? secondaryCombo_->currentData().toString() : QString();
    QString previousSide =
        secondarySideCombo_ != nullptr ? secondarySideCombo_->currentData().toString() : QString();

    if (previousPrimary.isEmpty() && hasSaved) {
        previousPrimary = saved.primary;
        previousSecondary = saved.secondary;
        previousSide = saved.side;
    }

    primaryCombo_->blockSignals(true);
    secondaryCombo_->blockSignals(true);
    primaryCombo_->clear();
    secondaryCombo_->clear();

    QString detectedPrimary;
    QStringList connectedNames;

    for (const MonitorInfo &monitor : monitors_) {
        if (!monitor.connected) {
            continue;
        }

        connectedNames.push_back(monitor.name);
        const QString label = QStringLiteral("%1 (%2×%3)")
                                  .arg(monitor.name)
                                  .arg(monitor.width)
                                  .arg(monitor.height);
        primaryCombo_->addItem(label, monitor.name);
        secondaryCombo_->addItem(label, monitor.name);

        if (monitor.primary) {
            detectedPrimary = monitor.name;
        }
    }

    if (detectedPrimary.isEmpty() && !connectedNames.isEmpty()) {
        detectedPrimary = connectedNames.first();
    }

    if (!previousPrimary.isEmpty() && connectedNames.contains(previousPrimary)) {
        detectedPrimary = previousPrimary;
    }

    const int primaryIndex = primaryCombo_->findData(detectedPrimary);
    primaryCombo_->setCurrentIndex(primaryIndex >= 0 ? primaryIndex : 0);

    QString secondary = previousSecondary;

    if (secondary.isEmpty() || secondary == detectedPrimary || !connectedNames.contains(secondary)) {
        secondary.clear();

        for (const QString &name : connectedNames) {
            if (name != detectedPrimary) {
                secondary = name;
                break;
            }
        }
    }

    if (!secondary.isEmpty()) {
        const int secondaryIndex = secondaryCombo_->findData(secondary);
        secondaryCombo_->setCurrentIndex(secondaryIndex >= 0 ? secondaryIndex : 0);
    }

    if (!previousSide.isEmpty()) {
        const int sideIndex = secondarySideCombo_->findData(previousSide);
        if (sideIndex >= 0) {
            secondarySideCombo_->setCurrentIndex(sideIndex);
        }
    }

    primaryCombo_->blockSignals(false);
    secondaryCombo_->blockSignals(false);

    const bool hasPair = connectedNames.size() >= 2;
    secondaryCombo_->setEnabled(hasPair);
    secondarySideCombo_->setEnabled(hasPair);
    applyButton_->setEnabled(!connectedNames.isEmpty());

    statusLabel_->setText(
        connectedNames.isEmpty()
            ? QStringLiteral("Nenhum monitor conectado.")
            : QStringLiteral("%1 monitor(es) conectado(s).").arg(connectedNames.size()));

    updateMonitorPreview();
}

void SettingsWindow::updateMonitorPreview()
{
    if (preview_ == nullptr) {
        return;
    }

    preview_->setMonitors(
        monitors_,
        primaryCombo_->currentData().toString(),
        secondaryCombo_->currentData().toString());
}

void SettingsWindow::applyMonitorLayout()
{
    const QString primary = primaryCombo_->currentData().toString();
    const QString secondary = secondaryCombo_->currentData().toString();
    const QString side = secondarySideCombo_->currentData().toString();

    if (primary.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Monitores"), QStringLiteral("Selecione o monitor padrão."));
        return;
    }

    EdenMonitors::Layout layout;
    layout.primary = primary;
    layout.side = side.isEmpty() ? QStringLiteral("right") : side;

    bool hasPrimary = false;
    bool hasSecondary = false;

    for (const MonitorInfo &monitor : monitors_) {
        if (monitor.name == primary && monitor.connected) {
            layout.primaryMode = monitor.mode;
            hasPrimary = true;
        }

        if (monitor.name == secondary && monitor.connected) {
            layout.secondary = secondary;
            layout.secondaryMode = monitor.mode;
            hasSecondary = true;
        }
    }

    if (!hasPrimary) {
        QMessageBox::warning(this, QStringLiteral("Monitores"), QStringLiteral("Monitor padrão inválido."));
        return;
    }

    if (!hasSecondary || secondary == primary) {
        layout.secondary.clear();
        layout.secondaryMode.clear();
    }

    if (!EdenMonitors::apply(layout)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Monitores"),
            QStringLiteral("Não foi possível aplicar a configuração."));
        return;
    }

    // Persist even if apply already saved; keeps UI selection durable.
    EdenMonitors::save(layout);

    statusLabel_->setText(QStringLiteral("Configuração aplicada e salva."));
    refreshMonitors();

    // xrandr settles asynchronously; refresh shell geometry a few times.
    EdenShell::relayout();
    QTimer::singleShot(150, qApp, []() { EdenShell::relayout(); });
    QTimer::singleShot(400, qApp, []() { EdenShell::relayout(); });
    QTimer::singleShot(900, qApp, []() { EdenShell::relayout(); });
}

void SettingsWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), kBg);
    painter.fillRect(QRect(0, 0, 210, height()), kPanel);
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshMonitors();
}

void SettingsWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }

    QWidget::keyPressEvent(event);
}
