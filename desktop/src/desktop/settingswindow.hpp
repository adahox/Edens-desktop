#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QPushButton;
class QResizeEvent;
class QSlider;
class QStackedWidget;

class SettingsWindow final : public QWidget
{
public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    static void open();
    static SettingsWindow *instance();

    struct MonitorInfo
    {
        QString name;
        QString mode;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        bool connected = false;
        bool primary = false;
    };

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void buildUi();
    void setSection(int section);
    void refreshMonitors();
    void applyMonitorLayout();
    void updateMonitorPreview();
    void loadDockControls();
    void previewDockSettings();
    void applyDockSettings();
    void loadWallpaperControls();
    void selectWallpaperPath(const QString &path);
    void chooseWallpaperFile();
    void applyWallpaperSettings();
    void resetWallpaperSettings();
    void loadAppearanceControls();
    void pushAppearance();
    void applyChrome();
    void filterNavigation(const QString &query);
    void updateWindowMask();
    [[nodiscard]] QVector<MonitorInfo> queryMonitors() const;
    [[nodiscard]] QWidget *buildMonitorsPage();
    [[nodiscard]] QWidget *buildDockPage();
    [[nodiscard]] QWidget *buildWallpaperPage();
    [[nodiscard]] QWidget *buildAppearancePage();

    QWidget *header_ = nullptr;
    QLabel *headerTitle_ = nullptr;
    QLineEdit *search_ = nullptr;
    QListWidget *nav_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLabel *subtitle_ = nullptr;
    class MonitorPreview;
    MonitorPreview *preview_ = nullptr;
    QComboBox *primaryCombo_ = nullptr;
    QComboBox *secondaryCombo_ = nullptr;
    QComboBox *secondarySideCombo_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *applyButton_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QVector<MonitorInfo> monitors_;

    QSlider *dockIconSizeSlider_ = nullptr;
    QSlider *dockHoverSizeSlider_ = nullptr;
    QSlider *dockFontSizeSlider_ = nullptr;
    QSlider *dockGapSlider_ = nullptr;
    QSlider *dockMarginSlider_ = nullptr;
    QLabel *dockIconSizeValue_ = nullptr;
    QLabel *dockHoverSizeValue_ = nullptr;
    QLabel *dockFontSizeValue_ = nullptr;
    QLabel *dockGapValue_ = nullptr;
    QLabel *dockMarginValue_ = nullptr;
    QLabel *dockStatusLabel_ = nullptr;
    QPushButton *dockApplyButton_ = nullptr;
    QPushButton *dockResetButton_ = nullptr;

    QLabel *wallpaperPreview_ = nullptr;
    QLabel *wallpaperPathLabel_ = nullptr;
    QLabel *wallpaperStatusLabel_ = nullptr;
    QListWidget *wallpaperGallery_ = nullptr;
    QPushButton *wallpaperChooseButton_ = nullptr;
    QPushButton *wallpaperApplyButton_ = nullptr;
    QPushButton *wallpaperResetButton_ = nullptr;
    QString wallpaperPendingPath_;

    QSlider *blurSlider_ = nullptr;
    QSlider *liquidSlider_ = nullptr;
    QLabel *blurValue_ = nullptr;
    QLabel *liquidValue_ = nullptr;
    QPushButton *darkSegment_ = nullptr;
    QPushButton *lightSegment_ = nullptr;
    QPoint dragOffset_;
    bool dragging_ = false;

    static SettingsWindow *active_;

    static constexpr int kWidth = 920;
    static constexpr int kHeight = 620;
};
