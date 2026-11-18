#include "mainwindow.hpp"
#include "processingworker.hpp"
#include "theme.hpp"

#include "../core/upscaler.hpp"
#include "../media/image_io.hpp"
#include "../media/video_processor.hpp"
#include "../media/gif_processor.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QImage>
#include <QGroupBox>

#include <algorithm>
#include <cmath>
#include <format>
#include <filesystem>

namespace {
template <typename... Args>
QString qfmt(std::format_string<Args...> fmt, Args&&... args) {
    return QString::fromStdString(std::format(fmt, std::forward<Args>(args)...));
}
} // anonymous namespace

namespace upscaler {

// ============================================================================
// Construction / Destruction
// ============================================================================

MainWindow::MainWindow(Upscaler* engine, QWidget* parent)
    : QMainWindow(parent)
    , m_engine(engine)
{
    qRegisterMetaType<upscaler::UpscaleSettings>("upscaler::UpscaleSettings");
    qRegisterMetaType<upscaler::GifSettings>("upscaler::GifSettings");

    setWindowTitle("CUDA Media Upscaler");
    resize(1280, 920);
    setMinimumSize(1000, 750);

    // Define tab media types
    m_tabs[0].mediaType = "image";
    m_tabs[1].mediaType = "video";
    m_tabs[2].mediaType = "gif";
    m_tabs[3].mediaType = "video2gif";
    m_tabs[4].mediaType = "gif2video";

    createUi();
    updateGpuInfo();

    // Initial log
    appendLog("CUDA Media Upscaler initialized", "HEADER");

    auto gpu = m_engine->get_gpu_info();
    if (gpu.available) {
        appendLog(QString("CUDA: Available (%1)").arg(QString::fromStdString(gpu.name)), "INFO");
    } else {
        appendLog("CUDA: Not available - CPU fallback mode", "WARNING");
    }

    if (VideoProcessor::check_ffmpeg()) {
        appendLog("ffmpeg: Available", "INFO");
    } else {
        appendLog("ffmpeg: Not found (video/gif processing unavailable)", "WARNING");
    }

    if (GifProcessor::check_gifsicle()) {
        appendLog("gifsicle: Available", "INFO");
    } else {
        appendLog("gifsicle: Not found (optional, for GIF optimization)", "WARNING");
    }
}

MainWindow::~MainWindow()
{
    if (m_workerThread) {
        if (m_worker) m_worker->cancel();
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
}

// ============================================================================
// Events
// ============================================================================

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_workerThread && m_workerThread->isRunning()) {
        if (m_worker) m_worker->cancel();
        m_workerThread->quit();
        if (!m_workerThread->wait(3000)) {
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updatePreview();
}

// ============================================================================
// UI Construction
// ============================================================================

void MainWindow::createUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Title + GPU row
    auto* headerRow = new QWidget(central);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto* titleLabel = new QLabel("CUDA Media Upscaler", headerRow);
    titleLabel->setObjectName("titleLabel");
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    m_gpuLabel = new QLabel("", headerRow);
    m_gpuLabel->setObjectName("secondaryLabel");
    headerLayout->addWidget(m_gpuLabel);

    rootLayout->addWidget(headerRow);

    // Big tab bar (browser-style)
    m_tabBar = new QTabBar(central);
    m_tabBar->setObjectName("browserTabBar");
    m_tabBar->setExpanding(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->addTab("  Image  ");
    m_tabBar->addTab("  Video  ");
    m_tabBar->addTab("   GIF   ");
    m_tabBar->addTab(" Video \u2192 GIF ");
    m_tabBar->addTab(" GIF \u2192 Video ");
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    rootLayout->addWidget(m_tabBar);

    // Content area: left (stacked controls + progress + buttons) | right (preview + log)
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    // --- Left column: controls ---
    auto* leftPanel = new QWidget(central);
    leftPanel->setFixedWidth(420);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(5);

    // Scroll area wrapping the stacked widget
    auto* scrollArea = new QScrollArea(leftPanel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scrollContent = new QWidget();
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 5, 0);
    scrollLayout->setSpacing(0);

    // Stacked widget for per-tab controls
    m_stack = new QStackedWidget(scrollContent);
    for (int i = 0; i < TAB_COUNT; ++i) {
        auto* page = buildTabPage(i);
        m_tabs[i].page = page;
        m_stack->addWidget(page);
    }
    scrollLayout->addWidget(m_stack);
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    leftLayout->addWidget(scrollArea, 1);

    // Progress section (shared)
    m_progressLabel = new QLabel("Ready", leftPanel);
    m_progressLabel->setObjectName("secondaryLabel");
    leftLayout->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(leftPanel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    leftLayout->addWidget(m_progressBar);

    m_frameCounterLabel = new QLabel("", leftPanel);
    m_frameCounterLabel->setObjectName("secondaryLabel");
    leftLayout->addWidget(m_frameCounterLabel);

    // Action buttons (shared)
    m_processBtn = new QPushButton("UPSCALE", leftPanel);
    m_processBtn->setObjectName("accentButton");
    m_processBtn->setCursor(Qt::PointingHandCursor);
    connect(m_processBtn, &QPushButton::clicked, this, &MainWindow::startProcessing);
    leftLayout->addWidget(m_processBtn);

    m_cancelBtn = new QPushButton("Cancel", leftPanel);
    m_cancelBtn->setObjectName("cancelButton");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancelProcessing);
    leftLayout->addWidget(m_cancelBtn);

    contentLayout->addWidget(leftPanel);

    // --- Right column: preview + log ---
    auto* rightPanel = new QWidget(central);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(5);

    auto* previewTitle = new QLabel("Preview", rightPanel);
    previewTitle->setObjectName("titleLabel");
    rightLayout->addWidget(previewTitle);

    m_previewLabel = new QLabel(rightPanel);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setText("No file selected");
    m_previewLabel->setStyleSheet(
        QString("background-color: %1; color: %2; font-size: 13pt; "
                "border: 1px solid %3; border-radius: 4px;")
            .arg(Theme::BG_MEDIUM, Theme::FG_SECONDARY, Theme::BORDER));
    m_previewLabel->setMinimumHeight(200);
    rightLayout->addWidget(m_previewLabel, 1);

    auto* logTitle = new QLabel("Operation Log", rightPanel);
    logTitle->setObjectName("titleLabel");
    rightLayout->addWidget(logTitle);

    m_logText = new QTextEdit(rightPanel);
    m_logText->setReadOnly(true);
    m_logText->setFixedHeight(200);
    rightLayout->addWidget(m_logText);

    contentLayout->addWidget(rightPanel, 1);
    rootLayout->addLayout(contentLayout, 1);
}

QWidget* MainWindow::buildTabPage(int tabIndex)
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(5, 10, 5, 5);
    layout->setSpacing(8);

    auto& t = m_tabs[tabIndex];
    const auto& mediaType = t.mediaType;

    // File selector
    QString btnText;
    if (mediaType == "image")      btnText = "Select Image";
    else if (mediaType == "video") btnText = "Select Video";
    else if (mediaType == "gif")   btnText = "Select GIF";
    else if (mediaType == "video2gif") btnText = "Select Video for GIF";
    else if (mediaType == "gif2video") btnText = "Select GIF for Video";

    t.fileBtn = new QPushButton(btnText, page);
    connect(t.fileBtn, &QPushButton::clicked, this, [this, tabIndex]() {
        selectFile(tabIndex);
    });
    layout->addWidget(t.fileBtn);
    m_inputWidgets.push_back(t.fileBtn);

    // FPS slider for V2G and G2V
    if (mediaType == "video2gif" || mediaType == "gif2video") {
        addFpsSection(layout, tabIndex);
    }

    // Scale + target resolution (every tab)
    addScaleSection(layout, tabIndex);

    // Interpolation + Sharpening (every tab)
    addInterpSharpenSection(layout, tabIndex);

    // GIF settings for GIF and V2G tabs (output format is a GIF)
    if (mediaType == "gif" || mediaType == "video2gif") {
        addGifSettingsSection(layout, tabIndex);
    }

    // Output format (per-tab, context-appropriate)
    auto* formatLabel = new QLabel("Output Format", page);
    layout->addWidget(formatLabel);

    t.formatCombo = new QComboBox(page);

    if (mediaType == "image") {
        t.formatCombo->addItems({"PNG", "JPG", "BMP", "WebP", "TIFF"});
    } else if (mediaType == "video") {
        t.formatCombo->addItems({"MP4", "MKV", "WebM", "AVI", "MOV"});
    } else if (mediaType == "gif") {
        t.formatCombo->addItems({"GIF"});
    } else if (mediaType == "video2gif") {
        t.formatCombo->addItems({"GIF"});
    } else if (mediaType == "gif2video") {
        t.formatCombo->addItems({"MP4", "MKV", "WebM", "AVI", "MOV"});
    }
    layout->addWidget(t.formatCombo);
    m_inputWidgets.push_back(t.formatCombo);

    layout->addStretch();
    return page;
}

void MainWindow::addScaleSection(QVBoxLayout* layout, int tabIndex)
{
    auto* parent = layout->parentWidget();
    auto& t = m_tabs[tabIndex];

    // Downscale checkbox + scale value label
    auto* modeRow = new QWidget(parent);
    auto* modeLayout = new QHBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 8, 0, 0);

    t.downscaleCheck = new QCheckBox("Invert (Downscale)", modeRow);
    connect(t.downscaleCheck, &QCheckBox::toggled, this, [this, tabIndex](bool checked) {
        onDownscaleToggled(tabIndex, checked);
    });
    modeLayout->addWidget(t.downscaleCheck);
    m_inputWidgets.push_back(t.downscaleCheck);

    modeLayout->addStretch();

    t.scaleValueLabel = new QLabel("2.0x Upscale", modeRow);
    t.scaleValueLabel->setObjectName("valueLabel");
    modeLayout->addWidget(t.scaleValueLabel);

    layout->addWidget(modeRow);

    // Scale slider (10-80 = 1.0x - 8.0x)
    t.scaleSlider = new QSlider(Qt::Horizontal, parent);
    t.scaleSlider->setRange(10, 80);
    t.scaleSlider->setValue(20);
    t.scaleSlider->setSingleStep(1);
    t.scaleSlider->setTickInterval(10);
    connect(t.scaleSlider, &QSlider::valueChanged, this, [this, tabIndex](int value) {
        onScaleSliderChanged(tabIndex, value);
    });
    layout->addWidget(t.scaleSlider);
    m_inputWidgets.push_back(t.scaleSlider);

    // Target resolution row
    auto* resRow = new QWidget(parent);
    auto* resLayout = new QHBoxLayout(resRow);
    resLayout->setContentsMargins(0, 2, 0, 0);

    auto* twLabel = new QLabel("Target W:", resRow);
    twLabel->setObjectName("secondaryLabel");
    resLayout->addWidget(twLabel);

    t.targetWEdit = new QLineEdit(resRow);
    t.targetWEdit->setFixedWidth(60);
    t.targetWEdit->setPlaceholderText("width");
    resLayout->addWidget(t.targetWEdit);
    m_inputWidgets.push_back(t.targetWEdit);

    auto* thLabel = new QLabel("H:", resRow);
    thLabel->setObjectName("secondaryLabel");
    resLayout->addWidget(thLabel);

    t.targetHEdit = new QLineEdit(resRow);
    t.targetHEdit->setFixedWidth(60);
    t.targetHEdit->setPlaceholderText("height");
    resLayout->addWidget(t.targetHEdit);
    m_inputWidgets.push_back(t.targetHEdit);

    auto* applyBtn = new QPushButton("Apply", resRow);
    applyBtn->setFixedWidth(60);
    connect(applyBtn, &QPushButton::clicked, this, [this, tabIndex]() {
        onApplyTargetResolution(tabIndex);
    });
    resLayout->addWidget(applyBtn);
    m_inputWidgets.push_back(applyBtn);

    resLayout->addStretch();
    layout->addWidget(resRow);

    // Resolution estimate label
    t.resolutionLabel = new QLabel("Output resolution: --", parent);
    t.resolutionLabel->setObjectName("secondaryLabel");
    layout->addWidget(t.resolutionLabel);
}

void MainWindow::addInterpSharpenSection(QVBoxLayout* layout, int tabIndex)
{
    auto* parent = layout->parentWidget();
    auto& t = m_tabs[tabIndex];

    // Interpolation
    auto* interpLabel = new QLabel("Interpolation", parent);
    layout->addWidget(interpLabel);

    t.interpCombo = new QComboBox(parent);
    t.interpCombo->addItems({"Nearest", "Bilinear", "Bicubic"});
    t.interpCombo->setCurrentIndex(2);
    layout->addWidget(t.interpCombo);
    m_inputWidgets.push_back(t.interpCombo);

    // Sharpening
    auto* sharpenLabel = new QLabel("Sharpening", parent);
    layout->addWidget(sharpenLabel);

    auto* sharpenRow = new QWidget(parent);
    auto* sharpenLayout = new QHBoxLayout(sharpenRow);
    sharpenLayout->setContentsMargins(0, 0, 0, 0);

    t.sharpenGroup = new QButtonGroup(this);
    int sid = 0;
    for (const auto& text : {"None", "Light", "Medium", "Strong"}) {
        auto* rb = new QRadioButton(text, sharpenRow);
        sharpenLayout->addWidget(rb);
        t.sharpenGroup->addButton(rb, sid++);
        m_inputWidgets.push_back(rb);
        if (sid == 1) rb->setChecked(true);
    }
    layout->addWidget(sharpenRow);
}

void MainWindow::addGifSettingsSection(QVBoxLayout* layout, int tabIndex)
{
    auto* parent = layout->parentWidget();
    auto& t = m_tabs[tabIndex];

    auto* gifGroup = new QGroupBox("GIF Quality", parent);
    auto* gifLayout = new QVBoxLayout(gifGroup);

    // Colors
    auto* colorsRow = new QWidget(gifGroup);
    auto* colorsLayout = new QHBoxLayout(colorsRow);
    colorsLayout->setContentsMargins(0, 0, 0, 0);

    auto* colorsLabel = new QLabel("Colors:", colorsRow);
    colorsLabel->setObjectName("secondaryLabel");
    colorsLayout->addWidget(colorsLabel);
    colorsLayout->addStretch();

    t.gifColorsCombo = new QComboBox(colorsRow);
    t.gifColorsCombo->addItems({"16", "32", "64", "128", "256"});
    t.gifColorsCombo->setCurrentIndex(4);
    t.gifColorsCombo->setFixedWidth(80);
    colorsLayout->addWidget(t.gifColorsCombo);
    m_inputWidgets.push_back(t.gifColorsCombo);

    gifLayout->addWidget(colorsRow);

    // Lossy
    auto* lossyRow = new QWidget(gifGroup);
    auto* lossyLayout = new QHBoxLayout(lossyRow);
    lossyLayout->setContentsMargins(0, 0, 0, 0);

    auto* lossyLabel = new QLabel("Lossy (0-200):", lossyRow);
    lossyLabel->setObjectName("secondaryLabel");
    lossyLayout->addWidget(lossyLabel);
    lossyLayout->addStretch();

    t.gifLossySpin = new QSpinBox(lossyRow);
    t.gifLossySpin->setRange(0, 200);
    t.gifLossySpin->setValue(0);
    t.gifLossySpin->setFixedWidth(80);
    lossyLayout->addWidget(t.gifLossySpin);
    m_inputWidgets.push_back(t.gifLossySpin);

    gifLayout->addWidget(lossyRow);

    // Gifsicle status
    t.gifsicleLabel = new QLabel("", gifGroup);
    t.gifsicleLabel->setObjectName("secondaryLabel");
    if (GifProcessor::check_gifsicle()) {
        t.gifsicleLabel->setText("gifsicle: Available");
        t.gifsicleLabel->setStyleSheet(QString("color: %1;").arg(Theme::SUCCESS));
    } else {
        t.gifsicleLabel->setText("gifsicle: Not found (sudo apt install gifsicle)");
        t.gifsicleLabel->setStyleSheet(QString("color: %1;").arg(Theme::WARNING));
    }
    gifLayout->addWidget(t.gifsicleLabel);

    layout->addWidget(gifGroup);
}

void MainWindow::addFpsSection(QVBoxLayout* layout, int tabIndex)
{
    auto* parent = layout->parentWidget();
    auto& t = m_tabs[tabIndex];

    QString label = (t.mediaType == "video2gif") ? "Output FPS:" : "Output Video FPS:";

    auto* fpsRow = new QWidget(parent);
    auto* fpsRowLayout = new QHBoxLayout(fpsRow);
    fpsRowLayout->setContentsMargins(0, 8, 0, 0);
    auto* fpsTextLabel = new QLabel(label, fpsRow);
    t.fpsValueLabel = new QLabel("15", fpsRow);
    t.fpsValueLabel->setObjectName("valueLabel");
    fpsRowLayout->addWidget(fpsTextLabel);
    fpsRowLayout->addStretch();
    fpsRowLayout->addWidget(t.fpsValueLabel);
    layout->addWidget(fpsRow);

    int defaultFps = (t.mediaType == "gif2video") ? 24 : 15;
    int maxFps = (t.mediaType == "gif2video") ? 60 : 30;

    t.fpsSlider = new QSlider(Qt::Horizontal, parent);
    t.fpsSlider->setRange(5, maxFps);
    t.fpsSlider->setValue(defaultFps);
    t.fpsValueLabel->setText(QString::number(defaultFps));
    connect(t.fpsSlider, &QSlider::valueChanged, this, [this, tabIndex](int value) {
        onFpsChanged(tabIndex, value);
    });
    layout->addWidget(t.fpsSlider);
    m_inputWidgets.push_back(t.fpsSlider);
}

// ============================================================================
// GPU Info
// ============================================================================

void MainWindow::updateGpuInfo()
{
    auto gpu = m_engine->get_gpu_info();
    if (gpu.available) {
        m_gpuLabel->setText(
            QString("GPU: %1  |  %2 / %3 GB")
                .arg(QString::fromStdString(gpu.name))
                .arg(gpu.free_gb, 0, 'f', 1)
                .arg(gpu.total_gb, 0, 'f', 1));
    } else {
        m_gpuLabel->setText("GPU: Not available - CPU fallback");
    }
}

// ============================================================================
// Tab Changed
// ============================================================================

void MainWindow::onTabChanged(int index)
{
    if (index < 0 || index >= TAB_COUNT) return;
    m_stack->setCurrentIndex(index);

    // Update process button text based on tab's downscale state
    auto& t = m_tabs[index];
    bool down = t.downscaleCheck && t.downscaleCheck->isChecked();
    m_processBtn->setText(down ? "DOWNSCALE" : "UPSCALE");

    // Update preview with current tab's file
    if (!t.currentFile.isEmpty() && t.originalSize.has_value()) {
        // If this tab has a loaded file, try to show its preview
        // (preview pixmap is overwritten on file select, so just show text info)
        auto [w, h] = t.originalSize.value();
        if (m_previewPixmap.isNull()) {
            m_previewLabel->setText(QString("%1: %2x%3").arg(t.mediaType).arg(w).arg(h));
        } else {
            updatePreview();
        }
    }
}

int MainWindow::currentTab() const
{
    return m_tabBar->currentIndex();
}

// ============================================================================
// File Selection
// ============================================================================

void MainWindow::selectFile(int tabIndex)
{
    auto& t = m_tabs[tabIndex];
    QString filter;
    QString title;

    if (t.mediaType == "image") {
        title = "Select Image";
        filter = "Image files (*.png *.jpg *.jpeg *.bmp *.tiff *.webp);;All files (*)";
    } else if (t.mediaType == "video") {
        title = "Select Video";
        filter = "Video files (*.mp4 *.mkv *.webm *.avi *.mov);;All files (*)";
    } else if (t.mediaType == "gif") {
        title = "Select GIF";
        filter = "GIF files (*.gif);;Animated images (*.gif *.apng *.webp);;All files (*)";
    } else if (t.mediaType == "video2gif") {
        title = "Select Video for GIF";
        filter = "Video files (*.mp4 *.mkv *.webm *.avi *.mov);;All files (*)";
    } else if (t.mediaType == "gif2video") {
        title = "Select GIF for Video";
        filter = "GIF files (*.gif);;Animated images (*.gif *.apng *.webp);;All files (*)";
    }

    auto path = QFileDialog::getOpenFileName(this, title, QString(), filter);
    if (path.isEmpty()) return;

    t.currentFile = path;
    appendLog("Selected: " + path.toStdString(), "INFO");

    // Load info / preview
    try {
        if (t.mediaType == "image" || t.mediaType == "gif" || t.mediaType == "gif2video") {
            auto img = ImageIO::load(std::filesystem::path(path.toStdString()));
            t.originalSize = std::make_pair(img.width, img.height);
            appendLog(std::format("{}x{} ({}ch)", img.width, img.height, img.channels), "INFO");

            QImage qimg = ImageIO::to_qimage(img);
            m_previewPixmap = QPixmap::fromImage(std::move(qimg));
            updatePreview();
        } else {
            // Video or video2gif: use ffprobe
            auto info = VideoProcessor::probe(std::filesystem::path(path.toStdString()));
            t.originalSize = std::make_pair(info.width, info.height);
            appendLog(std::format("Video: {}x{} @ {:.2f} fps, codec={}",
                                  info.width, info.height, info.fps, info.codec), "INFO");

            m_previewPixmap = QPixmap();
            m_previewLabel->setText(QString("%1x%2 @ %3 fps")
                                        .arg(info.width).arg(info.height)
                                        .arg(info.fps, 0, 'f', 1));
        }
        updateResolutionEstimate(tabIndex);
    } catch (const std::exception& e) {
        appendLog(std::string("Failed to load: ") + e.what(), "ERROR");
        t.originalSize.reset();
    }
}

// ============================================================================
// Settings Interactions (per-tab)
// ============================================================================

void MainWindow::onScaleSliderChanged(int tabIndex, int value)
{
    auto& t = m_tabs[tabIndex];
    float scale = value / 10.0f;
    bool downscale = t.downscaleCheck->isChecked();
    QString modeText = downscale ? "Downscale" : "Upscale";
    t.scaleValueLabel->setText(QString("%1x %2").arg(scale, 0, 'f', 1).arg(modeText));
    updateResolutionEstimate(tabIndex);
}

void MainWindow::onDownscaleToggled(int tabIndex, bool checked)
{
    auto& t = m_tabs[tabIndex];
    float scale = t.scaleSlider->value() / 10.0f;
    QString modeText = checked ? "Downscale" : "Upscale";
    t.scaleValueLabel->setText(QString("%1x %2").arg(scale, 0, 'f', 1).arg(modeText));

    // Update shared button if this is the active tab
    if (tabIndex == currentTab()) {
        m_processBtn->setText(checked ? "DOWNSCALE" : "UPSCALE");
    }
    updateResolutionEstimate(tabIndex);
}

void MainWindow::onApplyTargetResolution(int tabIndex)
{
    auto& t = m_tabs[tabIndex];
    if (!t.originalSize.has_value()) {
        appendLog("No file loaded - cannot apply target resolution", "WARNING");
        return;
    }

    bool wOk = false, hOk = false;
    int targetW = t.targetWEdit->text().toInt(&wOk);
    int targetH = t.targetHEdit->text().toInt(&hOk);

    auto [origW, origH] = t.originalSize.value();

    if (wOk && targetW > 0 && origW > 0) {
        float scale = static_cast<float>(targetW) / origW;
        scale = std::clamp(scale, 0.1f, 8.0f);
        bool isDown = scale < 1.0f;
        float sliderScale = isDown ? (1.0f / scale) : scale;
        t.downscaleCheck->setChecked(isDown);
        t.scaleSlider->setValue(static_cast<int>(std::round(sliderScale * 10)));
        appendLog(std::format("Applied target width {} -> scale {:.2f}x", targetW, scale), "INFO");
    } else if (hOk && targetH > 0 && origH > 0) {
        float scale = static_cast<float>(targetH) / origH;
        scale = std::clamp(scale, 0.1f, 8.0f);
        bool isDown = scale < 1.0f;
        float sliderScale = isDown ? (1.0f / scale) : scale;
        t.downscaleCheck->setChecked(isDown);
        t.scaleSlider->setValue(static_cast<int>(std::round(sliderScale * 10)));
        appendLog(std::format("Applied target height {} -> scale {:.2f}x", targetH, scale), "INFO");
    } else {
        appendLog("Enter a valid target width or height", "WARNING");
    }
}

void MainWindow::onFpsChanged(int tabIndex, int value)
{
    m_tabs[tabIndex].fpsValueLabel->setText(QString::number(value));
}

// ============================================================================
// Resolution Estimate
// ============================================================================

void MainWindow::updateResolutionEstimate(int tabIndex)
{
    auto& t = m_tabs[tabIndex];
    if (!t.originalSize.has_value()) {
        t.resolutionLabel->setText("Output resolution: --");
        return;
    }

    auto [origW, origH] = t.originalSize.value();
    float scale = t.scaleSlider->value() / 10.0f;
    if (t.downscaleCheck->isChecked()) {
        scale = 1.0f / scale;
    }

    int outW = static_cast<int>(origW * scale);
    int outH = static_cast<int>(origH * scale);

    t.resolutionLabel->setText(
        QString("Output: %1x%2 (from %3x%4)")
            .arg(outW).arg(outH).arg(origW).arg(origH));
}

// ============================================================================
// Preview
// ============================================================================

void MainWindow::updatePreview()
{
    if (m_previewPixmap.isNull()) return;

    auto labelSize = m_previewLabel->size();
    if (labelSize.width() < 10 || labelSize.height() < 10) return;

    auto scaled = m_previewPixmap.scaled(
        labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_previewLabel->setPixmap(scaled);
}

// ============================================================================
// Log
// ============================================================================

void MainWindow::appendLog(const QString& message, const QString& level)
{
    QString color;
    if (level == "INFO")         color = Theme::LOG_INFO;
    else if (level == "DEBUG")   color = Theme::LOG_DEBUG;
    else if (level == "WARNING") color = Theme::LOG_WARN;
    else if (level == "ERROR")   color = Theme::LOG_ERROR;
    else if (level == "HEADER")  color = Theme::LOG_HEADER;
    else                         color = Theme::FG_SECONDARY;

    bool isBold = (level == "HEADER");
    QString html = QString("<span style=\"color:%1;%2\">%3</span><br>")
                       .arg(color,
                            isBold ? "font-weight:bold;" : "",
                            message.toHtmlEscaped());

    m_logText->insertHtml(html);

    auto cursor = m_logText->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logText->setTextCursor(cursor);
    m_logText->ensureCursorVisible();
}

// ============================================================================
// Processing State
// ============================================================================

void MainWindow::setProcessingEnabled(bool processing)
{
    for (auto* w : m_inputWidgets) {
        w->setEnabled(!processing);
    }
    m_tabBar->setEnabled(!processing);
    m_processBtn->setEnabled(!processing);
    m_cancelBtn->setEnabled(processing);

    if (!processing) {
        m_progressBar->setValue(0);
        m_progressLabel->setText("Ready");
        m_frameCounterLabel->setText("");
    }
}

QString MainWindow::getSaveFilter(int tabIndex) const
{
    const auto& t = m_tabs[tabIndex];
    if (t.mediaType == "image") {
        return "PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;WebP (*.webp);;TIFF (*.tiff);;All files (*)";
    }
    if (t.mediaType == "gif") {
        return "GIF (*.gif);;All files (*)";
    }
    if (t.mediaType == "video2gif") {
        return "GIF (*.gif);;All files (*)";
    }
    if (t.mediaType == "gif2video") {
        return "MP4 (*.mp4);;MKV (*.mkv);;WebM (*.webm);;AVI (*.avi);;MOV (*.mov);;All files (*)";
    }
    // video
    return "MP4 (*.mp4);;MKV (*.mkv);;WebM (*.webm);;AVI (*.avi);;MOV (*.mov);;All files (*)";
}

QString MainWindow::getDefaultExtension(int tabIndex) const
{
    const auto& t = m_tabs[tabIndex];
    if (t.mediaType == "image") {
        QString fmt = t.formatCombo->currentText().toLower();
        if (fmt == "jpg") return ".jpg";
        return "." + fmt;
    }
    if (t.mediaType == "gif") return ".gif";
    if (t.mediaType == "video2gif") {
        QString fmt = t.formatCombo->currentText().toLower();
        return "." + fmt;
    }
    // video, gif2video
    QString fmt = t.formatCombo->currentText().toLower();
    return "." + fmt;
}

// ============================================================================
// Start Processing
// ============================================================================

void MainWindow::startProcessing()
{
    int idx = currentTab();
    auto& t = m_tabs[idx];

    if (t.currentFile.isEmpty()) {
        QMessageBox::warning(this, "No File", "Please select a file first.");
        return;
    }

    auto outputPath = QFileDialog::getSaveFileName(
        this, "Save Output", QString(), getSaveFilter(idx));
    if (outputPath.isEmpty()) return;

    auto ext = getDefaultExtension(idx);
    if (!outputPath.contains('.')) {
        outputPath += ext;
    }

    // Gather scale settings from this tab
    float scale = t.scaleSlider->value() / 10.0f;
    if (t.downscaleCheck->isChecked()) {
        scale = 1.0f / scale;
    }

    UpscaleSettings settings;
    settings.scale = scale;

    int interpIdx = t.interpCombo->currentIndex();
    switch (interpIdx) {
        case 0: settings.interpolation = Interpolation::Nearest;  break;
        case 1: settings.interpolation = Interpolation::Bilinear; break;
        default: settings.interpolation = Interpolation::Bicubic; break;
    }

    int sharpenId = t.sharpenGroup->checkedId();
    switch (sharpenId) {
        case 0: settings.sharpen = SharpenLevel::None;   break;
        case 1: settings.sharpen = SharpenLevel::Light;  break;
        case 2: settings.sharpen = SharpenLevel::Medium; break;
        case 3: settings.sharpen = SharpenLevel::Strong; break;
        default: settings.sharpen = SharpenLevel::None;  break;
    }

    // GIF settings (from this tab's widgets, if present)
    GifSettings gifSettings;
    if (t.gifColorsCombo) {
        gifSettings.colors = t.gifColorsCombo->currentText().toInt();
    }
    if (t.gifLossySpin) {
        gifSettings.lossy = t.gifLossySpin->value();
    }

    // Video codec / crf
    QString codec = "libx264";
    int crf = 18;
    QString format = t.formatCombo->currentText();
    if (format == "WebM") {
        codec = "libvpx-vp9"; crf = 30;
    } else if (format == "AVI") {
        codec = "mpeg4"; crf = 4;
    }

    // Start worker
    setProcessingEnabled(true);
    appendLog(std::string(50, '='), "HEADER");
    appendLog("Starting processing...", "HEADER");

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(1000);
        delete m_workerThread;
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    m_workerThread = new QThread(this);
    m_worker = new ProcessingWorker(m_engine);
    m_worker->reset();
    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &ProcessingWorker::progressUpdated,
            this, &MainWindow::onProgressUpdated);
    connect(m_worker, &ProcessingWorker::logMessage,
            this, &MainWindow::onLogMessage);
    connect(m_worker, &ProcessingWorker::finished,
            this, &MainWindow::onProcessingFinished);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();

    // Dispatch to the correct worker method
    if (t.mediaType == "image") {
        QMetaObject::invokeMethod(m_worker, "processImage", Qt::QueuedConnection,
                                  Q_ARG(QString, t.currentFile),
                                  Q_ARG(QString, outputPath),
                                  Q_ARG(upscaler::UpscaleSettings, settings));
    } else if (t.mediaType == "video") {
        QMetaObject::invokeMethod(m_worker, "processVideo", Qt::QueuedConnection,
                                  Q_ARG(QString, t.currentFile),
                                  Q_ARG(QString, outputPath),
                                  Q_ARG(upscaler::UpscaleSettings, settings),
                                  Q_ARG(QString, codec),
                                  Q_ARG(int, crf));
    } else if (t.mediaType == "gif") {
        QMetaObject::invokeMethod(m_worker, "processGif", Qt::QueuedConnection,
                                  Q_ARG(QString, t.currentFile),
                                  Q_ARG(QString, outputPath),
                                  Q_ARG(upscaler::UpscaleSettings, settings),
                                  Q_ARG(upscaler::GifSettings, gifSettings));
    } else if (t.mediaType == "video2gif") {
        int fps = t.fpsSlider ? t.fpsSlider->value() : 15;
        int width = t.originalSize.has_value() ? t.originalSize->first : -1;
        QMetaObject::invokeMethod(m_worker, "processVideoToGif", Qt::QueuedConnection,
                                  Q_ARG(QString, t.currentFile),
                                  Q_ARG(QString, outputPath),
                                  Q_ARG(upscaler::UpscaleSettings, settings),
                                  Q_ARG(upscaler::GifSettings, gifSettings),
                                  Q_ARG(int, fps),
                                  Q_ARG(int, width));
    } else if (t.mediaType == "gif2video") {
        int fps = t.fpsSlider ? t.fpsSlider->value() : 24;
        QMetaObject::invokeMethod(m_worker, "processGifToVideo", Qt::QueuedConnection,
                                  Q_ARG(QString, t.currentFile),
                                  Q_ARG(QString, outputPath),
                                  Q_ARG(upscaler::UpscaleSettings, settings),
                                  Q_ARG(QString, codec),
                                  Q_ARG(int, crf),
                                  Q_ARG(int, fps));
    }
}

void MainWindow::cancelProcessing()
{
    if (m_worker) {
        m_worker->cancel();
        appendLog("Cancellation requested...", "WARNING");
        m_cancelBtn->setEnabled(false);
    }
}

// ============================================================================
// Worker Signal Handlers
// ============================================================================

void MainWindow::onProgressUpdated(const QString& status, int percent,
                                   const QString& frameInfo)
{
    m_progressLabel->setText(status);
    m_progressBar->setValue(percent);
    if (!frameInfo.isEmpty()) {
        m_frameCounterLabel->setText(QString("Frame: %1").arg(frameInfo));
    }
}

void MainWindow::onLogMessage(const QString& message, const QString& level)
{
    appendLog(message, level);
}

void MainWindow::onProcessingFinished(bool success, const QString& message)
{
    setProcessingEnabled(false);

    if (success) {
        appendLog("Processing complete: " + message.toStdString(), "INFO");
        appendLog(std::string(50, '='), "HEADER");
        m_progressLabel->setText("Complete");
        m_progressBar->setValue(100);
    } else {
        appendLog("Processing failed: " + message.toStdString(), "ERROR");
        m_progressLabel->setText("Failed");
    }

    if (m_workerThread) {
        m_workerThread->quit();
    }
}

} // namespace upscaler
