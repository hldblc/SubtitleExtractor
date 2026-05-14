#include "MainWindow.h"

#include "CancellationToken.h"
#include "ExtractorFactory.h"
#include "SubtitleEntry.h"
#include "SubtitleWriter.h"
#include "UpdateChecker.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace subext {

// =======================================================================
// Whisper quality presets — pulled from huggingface.co/ggerganov/whisper.cpp
// Sizes are the actual .bin file sizes on Hugging Face (approximate; the
// real numbers vary by a few percent across model revisions).
// =======================================================================
namespace {
struct QualityPreset {
    const char* displayName;
    const char* filename;
    int         sizeMB;
    bool        multilingual;
};

constexpr QualityPreset kPresets[] = {
    { "Tiny (English, fastest, lowest accuracy)", "ggml-tiny.en.bin",         75,  false },
    { "Base (English, bundled, good default)",    "ggml-base.en.bin",        147,  false },
    { "Small (English)",                          "ggml-small.en.bin",       466,  false },
    { "Medium (English, slower, better)",         "ggml-medium.en.bin",     1500,  false },
    { "Large-v3-Turbo (multilingual, recommended)", "ggml-large-v3-turbo.bin", 1600, true  },
    { "Large-v3 (multilingual, best quality)",    "ggml-large-v3.bin",      3100,  true  },
};

constexpr const char* kHuggingfaceBase =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

// Whisper supports 99 languages; expose the top 21 in the dropdown.
// User-data string is the ISO-639-1 code Whisper expects in the
// `language=` filter option, plus "auto" for auto-detect.
struct Lang { const char* code; const char* display; };
constexpr Lang kLanguages[] = {
    { "auto", "Auto-detect"  }, { "en", "English"     },
    { "es",   "Spanish"      }, { "fr", "French"      },
    { "de",   "German"       }, { "it", "Italian"     },
    { "pt",   "Portuguese"   }, { "ru", "Russian"     },
    { "ja",   "Japanese"     }, { "ko", "Korean"      },
    { "zh",   "Chinese"      }, { "ar", "Arabic"      },
    { "hi",   "Hindi"        }, { "tr", "Turkish"     },
    { "pl",   "Polish"       }, { "nl", "Dutch"       },
    { "sv",   "Swedish"      }, { "cs", "Czech"       },
    { "da",   "Danish"       }, { "fi", "Finnish"     },
    { "no",   "Norwegian"    },
};
} // anonymous namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      cancelToken_(std::make_shared<CancellationToken>()) {
    setWindowTitle("Subtitle Extractor");
    resize(780, 460);  // compact simple-mode height; grows when Advanced is toggled
    setAcceptDrops(true);  // see dragEnterEvent / dropEvent overrides

    // Bundled via resources/app.qrc — falls back silently to default
    // Qt icon if the resource isn't present (icon.ico not provided yet).
    QIcon appIcon(":/icons/icon.ico");
    if (!appIcon.isNull()) setWindowIcon(appIcon);

    setupUi();
    connectSignals();
    styleProgressBar();

    // System tray icon for completion notifications. Many corporate /
    // locked-down Windows configs disable the tray entirely — guard.
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon_ = new QSystemTrayIcon(this);
        trayIcon_->setIcon(appIcon.isNull()
            ? style()->standardIcon(QStyle::SP_ComputerIcon)
            : appIcon);
        trayIcon_->setToolTip("Subtitle Extractor");
        trayIcon_->show();
    }

    // Pre-fill model field with auto-detected ggml*.bin (prefer largest).
    const QString defaultModel = findDefaultModelFile();
    if (!defaultModel.isEmpty()) {
        modelPathEdit_->setText(defaultModel);
    }

    onModeChanged(modeCombo_->currentIndex());
    loadSettings();  // restore last-used state, may override defaults above
    setStatus("Ready.");

    // Update check fires 2 seconds after window appears — non-blocking,
    // silent on failure (no popup for "you're offline").
    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::updateAvailable,
            this,           &MainWindow::onUpdateAvailable);
    connect(updateChecker_, &UpdateChecker::checkFailed, this,
            [this](const QString& reason) {
                appendLog("Update check failed: " + reason);
            });
    connect(updateChecker_, &UpdateChecker::noUpdateAvailable, this,
            [this](const QString& v) {
                appendLog("Update check: you're on the latest version (" + v + ").");
            });
    QTimer::singleShot(2000, updateChecker_, &UpdateChecker::checkForUpdate);
}

MainWindow::~MainWindow() = default;

// =======================================================================
// Auto-detect a Whisper model file at startup.
// Strategy: scan likely directories for ggml*.bin, pick the LARGEST file.
// Bigger ggml file = bigger model = more accurate transcription, so this
// is the right default heuristic.
// =======================================================================
QString MainWindow::modelsDirectory() {
    // %APPDATA%/Halit/Subtitle Extractor/models — writable per-user
    // location for downloaded models. AppData survives uninstall and is
    // shared across upgrades of the app.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/models";
}

QString MainWindow::findDefaultModelFile() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirs = {
        appDir,
        appDir + "/..",         // build/<Config>/.. -> build/
        appDir + "/../..",      // build/Release/../.. -> project root
        QDir::currentPath(),
        modelsDirectory(),      // downloaded models live here
    };

    QStringList candidates;
    for (const QString& dirPath : searchDirs) {
        QDir d(dirPath);
        if (!d.exists()) continue;
        const auto files = d.entryInfoList(
            QStringList{"ggml*.bin"}, QDir::Files);
        for (const QFileInfo& f : files) {
            candidates << f.absoluteFilePath();
        }
    }
    // De-duplicate (same file may resolve via multiple paths).
    candidates.removeDuplicates();
    if (candidates.isEmpty()) return {};

    // Sort by size DESC — bigger model wins.
    std::sort(candidates.begin(), candidates.end(),
              [](const QString& a, const QString& b) {
                  return QFileInfo(a).size() > QFileInfo(b).size();
              });
    return candidates.first();
}

void MainWindow::setupUi() {
    auto central = new QWidget(this);
    setCentralWidget(central);
    auto root = new QVBoxLayout(central);
    root->setSpacing(10);
    root->setContentsMargins(14, 14, 14, 14);

    // ----- Video queue -----
    auto queueGroup  = new QGroupBox(tr("Video queue"), this);
    auto queueLayout = new QVBoxLayout(queueGroup);
    videoList_ = new QListWidget;
    videoList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    videoList_->setMinimumHeight(140);
    queueLayout->addWidget(videoList_);

    auto queueBtnRow = new QHBoxLayout;
    addBtn_    = new QPushButton(tr("Add files"));
    removeBtn_ = new QPushButton(tr("Remove"));
    clearBtn_  = new QPushButton(tr("Clear"));
    queueBtnRow->addWidget(addBtn_);
    queueBtnRow->addWidget(removeBtn_);
    queueBtnRow->addWidget(clearBtn_);
    queueBtnRow->addStretch();
    queueLayout->addLayout(queueBtnRow);
    root->addWidget(queueGroup);

    // ----- Output folder -----
    auto outGroup = new QGroupBox(tr("Output folder"), this);
    auto outRow   = new QHBoxLayout(outGroup);
    outputDirEdit_ = new QLineEdit;
    outputDirEdit_->setPlaceholderText(
        tr("Folder where subtitle files will be saved"));
    outputBrowseBtn_ = new QPushButton(tr("Browse..."));
    openFolderBtn_   = new QPushButton(tr("Open"));
    openFolderBtn_->setToolTip(tr("Open this folder in Explorer"));
    outRow->addWidget(outputDirEdit_);
    outRow->addWidget(outputBrowseBtn_);
    outRow->addWidget(openFolderBtn_);
    root->addWidget(outGroup);

    // ----- Mode + model (advanced) -----
    modeGroup_ = new QGroupBox(tr("Extraction mode"), this);
    auto modeForm = new QFormLayout(modeGroup_);
    modeCombo_ = new QComboBox;
    modeCombo_->addItem(tr("Whisper - AI transcription of audio"),         "whisper");
    modeCombo_->addItem(tr("Embedded - extract existing subtitle track"), "embedded");
    modeCombo_->addItem(tr("OCR - read burned-in subtitles"),              "ocr");
    modeForm->addRow(tr("Mode:"), modeCombo_);

    // Quality preset — handles model picking for 95% of users.
    qualityCombo_ = new QComboBox;
    for (const auto& p : kPresets) {
        qualityCombo_->addItem(
            QString("%1 (~%2 MB)").arg(p.displayName).arg(p.sizeMB),
            QVariant(QString::fromLatin1(p.filename)));
    }
    qualityCombo_->addItem(tr("Custom .bin file..."), QVariant(QString()));
    qualityCombo_->setCurrentIndex(1);  // Base
    modeForm->addRow(tr("Quality:"), qualityCombo_);

    auto modelRow = new QHBoxLayout;
    modelPathEdit_ = new QLineEdit;
    modelPathEdit_->setPlaceholderText(tr("ggml-*.bin (Whisper only)"));
    modelBrowseBtn_ = new QPushButton(tr("Browse..."));
    modelRow->addWidget(modelPathEdit_);
    modelRow->addWidget(modelBrowseBtn_);
    modeForm->addRow(tr("Model:"), modelRow);

    // Language picker + translate-to-English toggle (Whisper only).
    languageCombo_ = new QComboBox;
    for (const auto& l : kLanguages) {
        languageCombo_->addItem(
            QString::fromLatin1(l.display),
            QVariant(QString::fromLatin1(l.code)));
    }
    modeForm->addRow(tr("Language:"), languageCombo_);

    translateCheck_ = new QCheckBox(tr("Translate to English"));
    translateCheck_->setToolTip(tr(
        "Whisper translates non-English audio to English text in one pass.\n"
        "Requires a multilingual model (Large-v3 or Large-v3-Turbo)."));
    modeForm->addRow(QString(), translateCheck_);

    root->addWidget(modeGroup_);

    // ----- Output formats (advanced) -----
    formatsGroup_ = new QGroupBox(tr("Output formats"), this);
    auto formatRow = new QHBoxLayout(formatsGroup_);
    srtCheck_ = new QCheckBox(tr("SRT"));
    vttCheck_ = new QCheckBox(tr("VTT"));
    txtCheck_ = new QCheckBox(tr("TXT"));
    srtCheck_->setChecked(true);
    formatRow->addWidget(srtCheck_);
    formatRow->addWidget(vttCheck_);
    formatRow->addWidget(txtCheck_);
    formatRow->addStretch();
    root->addWidget(formatsGroup_);

    // ----- Action row -----
    auto actionRow = new QHBoxLayout;
    extractButton_ = new QPushButton(tr("Extract"));
    extractButton_->setObjectName("extractButton");   // hooks into QSS selector
    extractButton_->setMinimumHeight(44);
    extractButton_->setMinimumWidth(140);
    actionRow->addWidget(extractButton_);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    actionRow->addWidget(progressBar_, 1);
    root->addLayout(actionRow);

    statusLabel_ = new QLabel(tr("Ready."));
    statusLabel_->setStyleSheet("color: #aaa; padding: 2px 4px;");
    root->addWidget(statusLabel_);

    // ----- Log area (advanced) -----
    logArea_ = new QTextEdit;
    logArea_->setReadOnly(true);
    logArea_->setFont(QFont("Consolas", 9));
    logArea_->setPlaceholderText(tr("Extraction log..."));
    root->addWidget(logArea_, 1);

    // ----- Bottom toolbar: advanced toggle + version -----
    auto bottomRow = new QHBoxLayout;
    bottomRow->addStretch();
    advancedToggle_ = new QCheckBox(tr("Advanced settings"));
    advancedToggle_->setChecked(false);
    bottomRow->addWidget(advancedToggle_);
    auto versionLabel = new QLabel("v" APP_VERSION);
    versionLabel->setStyleSheet("color: #666; padding-left: 12px;");
    bottomRow->addWidget(versionLabel);
    root->addLayout(bottomRow);

    // Animation drives smooth value transitions on the progress bar.
    progressAnim_ = new QPropertyAnimation(progressBar_, "value", this);
    progressAnim_->setDuration(180);
    progressAnim_->setEasingCurve(QEasingCurve::OutCubic);

    // Start in simple mode — friends see the minimal UI.
    onToggleAdvanced(false);
}

// =======================================================================
// Qt Style Sheets (QSS) — the chunk gets a continuous green gradient.
// Applying ANY chunk style switches Qt away from the native "blocky" Windows
// rendering to a single smooth bar. The QPropertyAnimation in setProgress()
// then makes the value transition itself smooth.
// =======================================================================
void MainWindow::styleProgressBar() {
    progressBar_->setStyleSheet(R"(
        QProgressBar {
            border: 1px solid palette(mid);
            border-radius: 4px;
            text-align: center;
            min-height: 22px;
            font-weight: bold;
            color: white;
        }
        QProgressBar::chunk {
            background-color: qlineargradient(
                spread:pad, x1:0, y1:0, x2:1, y2:0,
                stop:0 #2E7D32, stop:0.5 #43A047, stop:1 #66BB6A);
            border-radius: 3px;
        }
    )");
}

void MainWindow::connectSignals() {
    connect(addBtn_,          &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(removeBtn_,       &QPushButton::clicked, this, &MainWindow::onRemoveSelected);
    connect(clearBtn_,        &QPushButton::clicked, this, &MainWindow::onClearQueue);
    connect(modelBrowseBtn_,  &QPushButton::clicked, this, &MainWindow::onBrowseModel);
    connect(outputBrowseBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    connect(openFolderBtn_,   &QPushButton::clicked, this, &MainWindow::onOpenOutputFolder);
    connect(extractButton_,   &QPushButton::clicked, this, &MainWindow::onExtractOrCancelClicked);
    connect(modeCombo_,       QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &MainWindow::onModeChanged);
    connect(qualityCombo_,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &MainWindow::onQualityPresetChanged);
    connect(advancedToggle_,  &QCheckBox::toggled,
            this,             &MainWindow::onToggleAdvanced);
}

void MainWindow::onToggleAdvanced(bool show) {
    modeGroup_->setVisible(show);
    formatsGroup_->setVisible(show);
    logArea_->setVisible(show);
    // Grow the window to fit when revealing advanced — but only grow,
    // never shrink (respect any manual resize the user already did).
    if (show && height() < 720) {
        resize(width(), 720);
    }
}

void MainWindow::onAddFiles() {
    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add video files"), QString(),
        tr("Video files (*.mp4 *.mkv *.mov *.avi *.webm *.m4v);;All files (*.*)"));
    for (const auto& p : paths) addVideoPathToQueue(p);
}

void MainWindow::addVideoPathToQueue(const QString& path) {
    // Avoid duplicating an already-queued file.
    for (int i = 0; i < videoList_->count(); ++i) {
        if (videoList_->item(i)->text() == path) return;
    }
    videoList_->addItem(path);
    // Auto-fill output folder from the first added video's directory.
    if (outputDirEdit_->text().isEmpty()) {
        outputDirEdit_->setText(QFileInfo(path).absolutePath());
    }
}

// Accept the drag when at least one URL has a video-ish extension.
void MainWindow::dragEnterEvent(QDragEnterEvent* ev) {
    if (!ev->mimeData()->hasUrls()) return;
    static const QStringList kExts =
        { "mp4", "mkv", "mov", "avi", "webm", "m4v" };
    for (const QUrl& u : ev->mimeData()->urls()) {
        const QString suffix = QFileInfo(u.toLocalFile()).suffix().toLower();
        if (kExts.contains(suffix)) {
            ev->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* ev) {
    for (const QUrl& u : ev->mimeData()->urls()) {
        const QString local = u.toLocalFile();
        if (!local.isEmpty()) addVideoPathToQueue(local);
    }
    ev->acceptProposedAction();
}

void MainWindow::onRemoveSelected() {
    const auto sel = videoList_->selectedItems();
    for (auto* it : sel) delete videoList_->takeItem(videoList_->row(it));
}

void MainWindow::onClearQueue() { videoList_->clear(); }

void MainWindow::onBrowseModel() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select Whisper model file"), QString(),
        tr("Model files (*.bin);;All files (*.*)"));
    if (!path.isEmpty()) modelPathEdit_->setText(path);
}

void MainWindow::onBrowseOutput() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select output folder"));
    if (!dir.isEmpty()) outputDirEdit_->setText(dir);
}

void MainWindow::onOpenOutputFolder() {
    const QString dir = outputDirEdit_->text();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::information(this, tr("No folder"),
            tr("Pick an output folder first, or pick one that exists."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::onModeChanged(int index) {
    bool isWhisper = (modeCombo_->itemData(index).toString() == "whisper");
    modelPathEdit_->setEnabled(isWhisper);
    modelBrowseBtn_->setEnabled(isWhisper);
    qualityCombo_->setEnabled(isWhisper);
    languageCombo_->setEnabled(isWhisper);
    translateCheck_->setEnabled(isWhisper);
}

void MainWindow::onQualityPresetChanged(int index) {
    const QString filename = qualityCombo_->itemData(index).toString();
    if (filename.isEmpty()) {
        // "Custom .bin file..." — let user pick via Browse.
        modelPathEdit_->setEnabled(true);
        modelBrowseBtn_->setEnabled(true);
        modelPathEdit_->clear();
        return;
    }

    // Try the install dir first, then the user-data models dir.
    const QString appDirPath  = QCoreApplication::applicationDirPath() + "/" + filename;
    const QString userDirPath = modelsDirectory() + "/" + filename;
    QString chosen;
    if (QFileInfo::exists(appDirPath))       chosen = appDirPath;
    else if (QFileInfo::exists(userDirPath)) chosen = userDirPath;

    if (!chosen.isEmpty()) {
        modelPathEdit_->setText(chosen);
        return;
    }

    // Not present — offer to download.
    if (maybeOfferDownload(filename)) {
        modelPathEdit_->setText(modelsDirectory() + "/" + filename);
    }
}

// Download model file from Hugging Face into modelsDirectory(). Returns
// true on successful download.
bool MainWindow::maybeOfferDownload(const QString& modelFile) {
    int sizeMB = 0;
    for (const auto& p : kPresets) {
        if (modelFile == QString::fromLatin1(p.filename)) {
            sizeMB = p.sizeMB;
            break;
        }
    }
    const auto reply = QMessageBox::question(this, tr("Download model"),
        tr("The model file '%1' is not installed.\n\n"
           "Download from Hugging Face? (%2 MB)")
            .arg(modelFile).arg(sizeMB),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (reply != QMessageBox::Yes) return false;

    QDir().mkpath(modelsDirectory());
    const QString dest = modelsDirectory() + "/" + modelFile;
    const QUrl url(QString(kHuggingfaceBase) + modelFile);

    // Synchronous download with a progress dialog. For one-shot downloads
    // tied to a user action, a modal dialog is fine — the user explicitly
    // chose to wait. We DO process events so Cancel and the bar still work.
    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "SubtitleExtractor");
    QNetworkReply* nr = nam->get(req);

    QProgressDialog dlg(tr("Downloading %1...").arg(modelFile),
                        tr("Cancel"), 0, 100, this);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.show();

    connect(nr, &QNetworkReply::downloadProgress, &dlg,
            [&dlg](qint64 done, qint64 total) {
                if (total > 0) {
                    dlg.setMaximum(100);
                    dlg.setValue(static_cast<int>(done * 100 / total));
                }
            });
    connect(&dlg, &QProgressDialog::canceled, nr, &QNetworkReply::abort);

    QEventLoop loop;
    connect(nr, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = false;
    if (nr->error() == QNetworkReply::NoError) {
        QFile out(dest);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(nr->readAll());
            out.close();
            success = true;
        }
    } else if (nr->error() != QNetworkReply::OperationCanceledError) {
        QMessageBox::warning(this, tr("Download failed"),
            tr("Could not download %1:\n%2")
                .arg(modelFile, nr->errorString()));
    }
    nr->deleteLater();
    nam->deleteLater();
    dlg.close();
    return success;
}

void MainWindow::appendLog(const QString& msg) { logArea_->append(msg); }

void MainWindow::setProgress(int percent) {
    // Smooth animated transition from current value to new value.
    progressAnim_->stop();
    progressAnim_->setStartValue(progressBar_->value());
    progressAnim_->setEndValue(percent);
    progressAnim_->start();
}

void MainWindow::setStatus(const QString& t) { statusLabel_->setText(t); }

void MainWindow::setRunningState(bool running) {
    isRunning_ = running;
    extractButton_->setText(running ? tr("Cancel") : tr("Extract"));
    addBtn_->setEnabled(!running);
    removeBtn_->setEnabled(!running);
    clearBtn_->setEnabled(!running);
    modeCombo_->setEnabled(!running);
    modelBrowseBtn_->setEnabled(!running && modeCombo_->currentData().toString() == "whisper");
    outputBrowseBtn_->setEnabled(!running);
}

void MainWindow::onUpdateAvailable(const QString& latestVersion,
                                    const QString& downloadUrl) {
    appendLog(QString("Update available: %1 (you have %2).")
                  .arg(latestVersion, QString::fromLatin1(APP_VERSION)));
    const auto reply = QMessageBox::question(
        this, tr("Update available"),
        tr("Version %1 is available.\nYou are currently on %2.\n\n"
           "Open the download page now?")
            .arg(latestVersion, QString::fromLatin1(APP_VERSION)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (reply == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(downloadUrl));
    }
}

void MainWindow::onBatchFinished(int successCount, int failCount, bool cancelled) {
    setRunningState(false);
    setProgress(cancelled ? 0 : 100);

    QString summary;
    QSystemTrayIcon::MessageIcon trayLevel = QSystemTrayIcon::Information;
    if (cancelled) {
        summary = tr("Cancelled after %1 successful, %2 failed.")
                      .arg(successCount).arg(failCount);
        trayLevel = QSystemTrayIcon::Warning;
    } else if (failCount == 0) {
        summary = tr("Done - all %1 file(s) succeeded.").arg(successCount);
    } else {
        summary = tr("Finished - %1 ok, %2 failed.")
                      .arg(successCount).arg(failCount);
        trayLevel = QSystemTrayIcon::Warning;
    }
    setStatus(summary);

    // Tray notification when the window isn't focused. Avoids interrupting
    // the user with a popup if they're already looking at the app.
    if (trayIcon_ && !isActiveWindow()) {
        trayIcon_->showMessage(tr("Subtitle Extractor"), summary, trayLevel, 5000);
    } else {
        QMessageBox::information(this, tr("Batch finished"), summary);
    }
}

void MainWindow::onExtractOrCancelClicked() {
    if (isRunning_) {
        appendLog("Cancellation requested...");
        cancelToken_->cancel();
        extractButton_->setEnabled(false);
        return;
    }

    if (videoList_->count() == 0) {
        QMessageBox::warning(this, tr("Empty queue"),
                             tr("Add at least one video to the queue."));
        return;
    }
    if (outputDirEdit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing output"),
                             tr("Please choose an output folder."));
        return;
    }
    const QString modeStr = modeCombo_->currentData().toString();
    if (modeStr == "whisper" && modelPathEdit_->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing model"),
                             tr("Whisper mode needs a model (.bin) file."));
        return;
    }
    QStringList formats;
    if (srtCheck_->isChecked()) formats << "srt";
    if (vttCheck_->isChecked()) formats << "vtt";
    if (txtCheck_->isChecked()) formats << "txt";
    if (formats.isEmpty()) {
        QMessageBox::warning(this, tr("No format"), tr("Pick at least one output format."));
        return;
    }

    BatchRequest req;
    for (int i = 0; i < videoList_->count(); ++i) {
        req.videoPaths << videoList_->item(i)->text();
    }
    req.outputDir = outputDirEdit_->text();
    req.modelPath = modelPathEdit_->text();
    req.formats   = formats;
    req.modeStr   = modeStr;
    req.language  = languageCombo_->currentData().toString();
    req.translate = translateCheck_->isChecked();

    logArea_->clear();
    appendLog(QString("Starting batch of %1 file(s) [%2 mode]...")
                  .arg(req.videoPaths.size()).arg(modeStr));

    cancelToken_->reset();
    setRunningState(true);
    extractButton_->setEnabled(true);
    setProgress(0);

    auto cancel = cancelToken_;
    (void)QtConcurrent::run([this, req, cancel]() {
        runBatchOnWorker(req, cancel);
    });
}

// =======================================================================
// Worker thread body. Runs OFF the GUI thread; must never touch widgets
// directly — everything funnels through QMetaObject::invokeMethod.
// =======================================================================
void MainWindow::runBatchOnWorker(const BatchRequest& req,
                                   std::shared_ptr<CancellationToken> cancel) {
    int successCount = 0;
    int failCount    = 0;
    const int total  = req.videoPaths.size();

    for (int i = 0; i < total; ++i) {
        if (cancel->isCancelled()) break;

        const QString videoPath = req.videoPaths[i];
        const QString baseName  = QFileInfo(videoPath).completeBaseName();
        const QString header    = QString("[%1/%2] %3").arg(i + 1).arg(total).arg(baseName);

        QMetaObject::invokeMethod(this, [this, header]() {
            appendLog("---- " + header + " ----");
            setStatus(header + tr(" - starting..."));
            setProgress(0);
        }, Qt::QueuedConnection);

        try {
            FactoryOptions opts;
            opts.mode = ExtractorFactory::parseMode(req.modeStr.toStdString());
            opts.whisperModelPath = req.modelPath.toStdString();
            if (!req.language.isEmpty()) {
                opts.whisperLanguage = req.language.toStdString();
            }
            opts.whisperTranslate = req.translate;
            std::unique_ptr<IExtractor> extractor = ExtractorFactory::create(opts);

            extractor->setCancellationToken(cancel.get());
            extractor->setProgressCallback([this, i, total, header](int pct) {
                QMetaObject::invokeMethod(this, [this, i, total, header, pct]() {
                    int overall = (i * 100 + pct) / total;
                    setProgress(overall);
                    setStatus(QString("%1 - %2%").arg(header).arg(pct));
                }, Qt::QueuedConnection);
            });

            auto entries = extractor->extract(videoPath.toStdString());
            if (cancel->isCancelled()) break;

            QStringList written;
            std::filesystem::path outBase =
                std::filesystem::path(req.outputDir.toStdString()) /
                baseName.toStdString();
            for (const auto& fmt : req.formats) {
                std::filesystem::path out = outBase;
                out += ("." + fmt.toStdString());
                if      (fmt == "srt") SubtitleWriter::writeSrt(entries, out);
                else if (fmt == "vtt") SubtitleWriter::writeVtt(entries, out);
                else if (fmt == "txt") SubtitleWriter::writeTxt(entries, out);
                written << QString::fromStdString(out.string());
            }

            const auto entriesCount = static_cast<int>(entries.size());
            ++successCount;
            QMetaObject::invokeMethod(this, [this, header, entriesCount, written]() {
                appendLog(header + QString(" OK - %1 entries").arg(entriesCount));
                for (const auto& w : written) appendLog("    -> " + w);
            }, Qt::QueuedConnection);

        } catch (const std::exception& ex) {
            const QString msg = QString::fromStdString(ex.what());
            ++failCount;
            QMetaObject::invokeMethod(this, [this, header, msg]() {
                appendLog(header + " FAILED: " + msg);
            }, Qt::QueuedConnection);
        }
    }

    const bool wasCancelled = cancel->isCancelled();
    QMetaObject::invokeMethod(this, [this, successCount, failCount, wasCancelled]() {
        onBatchFinished(successCount, failCount, wasCancelled);
    }, Qt::QueuedConnection);
}

// =======================================================================
// QSettings persistence — values land in
//   HKEY_CURRENT_USER\Software\Halit\Subtitle Extractor
// because gui_main.cpp sets organization + application name.
// =======================================================================
void MainWindow::saveSettings() {
    QSettings s;
    s.setValue("window/geometry",  saveGeometry());
    s.setValue("output/folder",    outputDirEdit_->text());
    s.setValue("mode/current",     modeCombo_->currentData().toString());
    s.setValue("model/path",       modelPathEdit_->text());
    s.setValue("model/qualityIdx", qualityCombo_->currentIndex());
    s.setValue("model/language",   languageCombo_->currentData().toString());
    s.setValue("model/translate",  translateCheck_->isChecked());
    s.setValue("formats/srt",      srtCheck_->isChecked());
    s.setValue("formats/vtt",      vttCheck_->isChecked());
    s.setValue("formats/txt",      txtCheck_->isChecked());
    s.setValue("ui/advanced",      advancedToggle_->isChecked());
}

void MainWindow::loadSettings() {
    QSettings s;

    if (s.contains("window/geometry")) {
        restoreGeometry(s.value("window/geometry").toByteArray());
    }
    if (s.contains("output/folder")) {
        outputDirEdit_->setText(s.value("output/folder").toString());
    }
    if (s.contains("mode/current")) {
        const QString mode = s.value("mode/current").toString();
        const int idx = modeCombo_->findData(mode);
        if (idx >= 0) modeCombo_->setCurrentIndex(idx);
    }
    if (s.contains("model/qualityIdx")) {
        const int idx = s.value("model/qualityIdx").toInt();
        if (idx >= 0 && idx < qualityCombo_->count()) {
            qualityCombo_->setCurrentIndex(idx);
        }
    }
    // model/path overrides quality preset's auto-filled value.
    if (s.contains("model/path")) {
        const QString p = s.value("model/path").toString();
        if (!p.isEmpty() && QFileInfo::exists(p)) {
            modelPathEdit_->setText(p);
        }
    }
    if (s.contains("model/language")) {
        const int idx = languageCombo_->findData(s.value("model/language").toString());
        if (idx >= 0) languageCombo_->setCurrentIndex(idx);
    }
    if (s.contains("model/translate")) {
        translateCheck_->setChecked(s.value("model/translate").toBool());
    }
    if (s.contains("formats/srt")) srtCheck_->setChecked(s.value("formats/srt").toBool());
    if (s.contains("formats/vtt")) vttCheck_->setChecked(s.value("formats/vtt").toBool());
    if (s.contains("formats/txt")) txtCheck_->setChecked(s.value("formats/txt").toBool());

    if (s.contains("ui/advanced") && s.value("ui/advanced").toBool()) {
        advancedToggle_->setChecked(true);  // triggers onToggleAdvanced -> resize
    }
}

void MainWindow::closeEvent(QCloseEvent* ev) {
    saveSettings();
    QMainWindow::closeEvent(ev);
}

} // namespace subext
