#include "MainWindow.h"

#include "CancellationToken.h"
#include "ExtractorFactory.h"
#include "SubtitleEntry.h"
#include "SubtitleWriter.h"
#include "UpdateChecker.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QIcon>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace subext {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      cancelToken_(std::make_shared<CancellationToken>()) {
    setWindowTitle("Subtitle Extractor");
    resize(820, 700);

    // Bundled via resources/app.qrc — falls back silently to default
    // Qt icon if the resource isn't present (icon.ico not provided yet).
    QIcon appIcon(":/icons/icon.ico");
    if (!appIcon.isNull()) setWindowIcon(appIcon);

    setupUi();
    connectSignals();
    styleProgressBar();

    // Pre-fill model field with auto-detected ggml*.bin (prefer largest).
    const QString defaultModel = findDefaultModelFile();
    if (!defaultModel.isEmpty()) {
        modelPathEdit_->setText(defaultModel);
    }

    onModeChanged(modeCombo_->currentIndex());
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
QString MainWindow::findDefaultModelFile() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirs = {
        appDir,
        appDir + "/..",         // build/<Config>/.. -> build/
        appDir + "/../..",      // build/Release/../.. -> project root
        QDir::currentPath(),
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

    // ----- Video queue -----
    auto queueGroup = new QGroupBox(tr("Video queue"), this);
    auto queueLayout = new QVBoxLayout(queueGroup);

    videoList_ = new QListWidget;
    videoList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    videoList_->setMinimumHeight(110);
    queueLayout->addWidget(videoList_);

    auto queueBtnRow = new QHBoxLayout;
    addBtn_    = new QPushButton(tr("Add files..."));
    removeBtn_ = new QPushButton(tr("Remove selected"));
    clearBtn_  = new QPushButton(tr("Clear"));
    queueBtnRow->addWidget(addBtn_);
    queueBtnRow->addWidget(removeBtn_);
    queueBtnRow->addWidget(clearBtn_);
    queueBtnRow->addStretch();
    queueLayout->addLayout(queueBtnRow);

    root->addWidget(queueGroup);

    // ----- Mode & model -----
    auto modeGroup = new QGroupBox(tr("Extraction mode"), this);
    auto modeForm  = new QFormLayout(modeGroup);

    modeCombo_ = new QComboBox;
    modeCombo_->addItem(tr("Whisper - AI transcription of audio"),         "whisper");
    modeCombo_->addItem(tr("Embedded - extract existing subtitle track"), "embedded");
    modeCombo_->addItem(tr("OCR - read burned-in subtitles"),              "ocr");
    modeForm->addRow(tr("Mode:"), modeCombo_);

    auto modelRow = new QHBoxLayout;
    modelPathEdit_ = new QLineEdit;
    modelPathEdit_->setPlaceholderText(tr("ggml-*.bin (Whisper only)"));
    modelBrowseBtn_ = new QPushButton(tr("Browse..."));
    modelRow->addWidget(modelPathEdit_);
    modelRow->addWidget(modelBrowseBtn_);
    modeForm->addRow(tr("Model:"), modelRow);

    root->addWidget(modeGroup);

    // ----- Output -----
    auto outputGroup = new QGroupBox(tr("Output"), this);
    auto outputForm  = new QFormLayout(outputGroup);

    auto outputRow = new QHBoxLayout;
    outputDirEdit_ = new QLineEdit;
    outputDirEdit_->setPlaceholderText(tr("Folder - each video gets its base name + chosen extensions"));
    outputBrowseBtn_ = new QPushButton(tr("Browse..."));
    outputRow->addWidget(outputDirEdit_);
    outputRow->addWidget(outputBrowseBtn_);
    outputForm->addRow(tr("Folder:"), outputRow);

    auto formatRow = new QHBoxLayout;
    srtCheck_ = new QCheckBox(tr("SRT"));
    vttCheck_ = new QCheckBox(tr("VTT"));
    txtCheck_ = new QCheckBox(tr("TXT"));
    srtCheck_->setChecked(true);
    formatRow->addWidget(srtCheck_);
    formatRow->addWidget(vttCheck_);
    formatRow->addWidget(txtCheck_);
    formatRow->addStretch();
    outputForm->addRow(tr("Formats:"), formatRow);

    root->addWidget(outputGroup);

    // ----- Action row -----
    auto actionRow = new QHBoxLayout;
    extractButton_ = new QPushButton(tr("Extract"));
    extractButton_->setMinimumHeight(36);
    actionRow->addWidget(extractButton_);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    actionRow->addWidget(progressBar_, 1);
    root->addLayout(actionRow);

    statusLabel_ = new QLabel(tr("Ready."));
    root->addWidget(statusLabel_);

    logArea_ = new QTextEdit;
    logArea_->setReadOnly(true);
    logArea_->setFont(QFont("Consolas", 9));
    logArea_->setPlaceholderText(tr("Extraction log will appear here..."));
    root->addWidget(logArea_, 1);

    // Animation drives smooth value transitions on the progress bar.
    progressAnim_ = new QPropertyAnimation(progressBar_, "value", this);
    progressAnim_->setDuration(180);   // ms per update
    progressAnim_->setEasingCurve(QEasingCurve::OutCubic);
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
    connect(extractButton_,   &QPushButton::clicked, this, &MainWindow::onExtractOrCancelClicked);
    connect(modeCombo_,       QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &MainWindow::onModeChanged);
}

void MainWindow::onAddFiles() {
    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add video files"), QString(),
        tr("Video files (*.mp4 *.mkv *.mov *.avi *.webm *.m4v);;All files (*.*)"));
    for (const auto& p : paths) videoList_->addItem(p);
    if (outputDirEdit_->text().isEmpty() && !paths.isEmpty()) {
        outputDirEdit_->setText(QFileInfo(paths.first()).absolutePath());
    }
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

void MainWindow::onModeChanged(int index) {
    bool isWhisper = (modeCombo_->itemData(index).toString() == "whisper");
    modelPathEdit_->setEnabled(isWhisper);
    modelBrowseBtn_->setEnabled(isWhisper);
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
    if (cancelled) {
        summary = tr("Cancelled after %1 successful, %2 failed.")
                      .arg(successCount).arg(failCount);
    } else if (failCount == 0) {
        summary = tr("Done - all %1 file(s) succeeded.").arg(successCount);
    } else {
        summary = tr("Finished - %1 ok, %2 failed.")
                      .arg(successCount).arg(failCount);
    }
    setStatus(summary);
    QMessageBox::information(this, tr("Batch finished"), summary);
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

} // namespace subext
