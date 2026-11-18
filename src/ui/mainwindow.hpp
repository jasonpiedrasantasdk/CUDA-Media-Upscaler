#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTabBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QButtonGroup>
#include <QThread>
#include <QPixmap>
#include <QCloseEvent>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../core/types.hpp"
#include "../media/gif_processor.hpp"

// Register custom types for cross-thread signal/slot
Q_DECLARE_METATYPE(upscaler::UpscaleSettings)
Q_DECLARE_METATYPE(upscaler::GifSettings)

namespace upscaler {

class Upscaler;
class ProcessingWorker;

/// Per-tab data: each tab has its own file selector, format combo, and optional extras.
struct TabPageData {
    QString mediaType;              // "image", "video", "gif", "video2gif", "gif2video"
    QWidget* page = nullptr;        // The stacked-widget page
    QPushButton* fileBtn = nullptr;
    QComboBox* formatCombo = nullptr;

    // Scale + mode (per-tab)
    QCheckBox* downscaleCheck = nullptr;
    QSlider* scaleSlider = nullptr;
    QLabel* scaleValueLabel = nullptr;
    QLineEdit* targetWEdit = nullptr;
    QLineEdit* targetHEdit = nullptr;
    QLabel* resolutionLabel = nullptr;

    // Interpolation + Sharpening (per-tab)
    QComboBox* interpCombo = nullptr;
    QButtonGroup* sharpenGroup = nullptr;

    // Optional: FPS (V2G, G2V)
    QSlider* fpsSlider = nullptr;
    QLabel* fpsValueLabel = nullptr;

    // Optional: GIF settings (GIF, V2G, G2V)
    QComboBox* gifColorsCombo = nullptr;
    QSpinBox* gifLossySpin = nullptr;
    QLabel* gifsicleLabel = nullptr;

    // Current file info
    QString currentFile;
    std::optional<std::pair<int, int>> originalSize;
};

/// Main application window with browser-style big tabs.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Upscaler* engine, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTabChanged(int index);
    void selectFile(int tabIndex);
    void onScaleSliderChanged(int tabIndex, int value);
    void onDownscaleToggled(int tabIndex, bool checked);
    void onApplyTargetResolution(int tabIndex);
    void onFpsChanged(int tabIndex, int value);

    void startProcessing();
    void cancelProcessing();

    void onProgressUpdated(const QString& status, int percent, const QString& frameInfo);
    void onLogMessage(const QString& message, const QString& level);
    void onProcessingFinished(bool success, const QString& message);

private:
    // UI construction
    void createUi();
    QWidget* buildTabPage(int tabIndex);
    void addScaleSection(QVBoxLayout* layout, int tabIndex);
    void addInterpSharpenSection(QVBoxLayout* layout, int tabIndex);
    void addGifSettingsSection(QVBoxLayout* layout, int tabIndex);
    void addFpsSection(QVBoxLayout* layout, int tabIndex);

    // Helpers
    void updateGpuInfo();
    void updateResolutionEstimate(int tabIndex);
    void updatePreview();
    void appendLog(const QString& message, const QString& level = QStringLiteral("INFO"));
    void appendLog(const std::string& message, const char* level = "INFO") {
        appendLog(QString::fromStdString(message), QString::fromLatin1(level));
    }
    void setProcessingEnabled(bool processing);
    QString getSaveFilter(int tabIndex) const;
    QString getDefaultExtension(int tabIndex) const;
    int currentTab() const;
    TabPageData& tab(int i) { return m_tabs[i]; }
    const TabPageData& tab(int i) const { return m_tabs[i]; }

    // Data
    Upscaler* m_engine;
    QPixmap m_previewPixmap;

    // Worker thread
    QThread* m_workerThread = nullptr;
    ProcessingWorker* m_worker = nullptr;

    // Widgets
    QLabel* m_gpuLabel = nullptr;
    QTabBar* m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;

    // Shared: progress + buttons
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_progressLabel = nullptr;
    QLabel* m_frameCounterLabel = nullptr;
    QPushButton* m_processBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    // Shared: preview + log
    QLabel* m_previewLabel = nullptr;
    QTextEdit* m_logText = nullptr;

    // Tab pages
    static constexpr int TAB_COUNT = 5;
    TabPageData m_tabs[TAB_COUNT];

    // All input widgets for enable/disable
    std::vector<QWidget*> m_inputWidgets;
};

} // namespace upscaler
