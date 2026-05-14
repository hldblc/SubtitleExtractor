#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <memory>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QSystemTrayIcon;

class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QTextEdit;
class QProgressBar;
class QLabel;
class QListWidget;
class QPropertyAnimation;
class QGroupBox;

namespace subext {

class CancellationToken;
class UpdateChecker;

// Inputs handed to the worker thread when a batch starts. Plain old data —
// snapshotted from the GUI at click-time so the worker never touches widgets.
struct BatchRequest {
    QStringList videoPaths;
    QString     outputDir;
    QString     modelPath;
    QStringList formats;
    QString     modeStr;
    QString     language;     // ISO-639-1 code or "auto"
    bool        translate{false};
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onClearQueue();
    void onBrowseModel();
    void onBrowseOutput();
    void onOpenOutputFolder();
    void onExtractOrCancelClicked();
    void onModeChanged(int index);
    void onQualityPresetChanged(int index);
    void appendLog(const QString& msg);
    void setProgress(int percent);
    void setStatus(const QString& text);
    void onBatchFinished(int successCount, int failCount, bool cancelled);
    void onUpdateAvailable(const QString& latestVersion, const QString& downloadUrl);
    void onToggleAdvanced(bool show);

private:
    void setupUi();
    void connectSignals();
    void styleProgressBar();
    void setRunningState(bool running);
    void runBatchOnWorker(const BatchRequest& req,
                          std::shared_ptr<CancellationToken> cancel);
    void addVideoPathToQueue(const QString& path);
    bool maybeOfferDownload(const QString& modelFile);
    void saveSettings();
    void loadSettings();

    static QString findDefaultModelFile();
    static QString modelsDirectory();        // %APPDATA%/Halit/Subtitle Extractor/models

    QListWidget*  videoList_       = nullptr;
    QPushButton*  addBtn_          = nullptr;
    QPushButton*  removeBtn_       = nullptr;
    QPushButton*  clearBtn_        = nullptr;

    QLineEdit*    modelPathEdit_   = nullptr;
    QLineEdit*    outputDirEdit_   = nullptr;
    QComboBox*    modeCombo_       = nullptr;
    QCheckBox*    srtCheck_        = nullptr;
    QCheckBox*    vttCheck_        = nullptr;
    QCheckBox*    txtCheck_        = nullptr;
    QPushButton*  extractButton_   = nullptr;
    QProgressBar* progressBar_     = nullptr;
    QTextEdit*    logArea_         = nullptr;
    QLabel*       statusLabel_     = nullptr;
    QPushButton*  modelBrowseBtn_  = nullptr;
    QPushButton*  outputBrowseBtn_ = nullptr;
    QPushButton*  openFolderBtn_   = nullptr;

    // Whisper quality + language controls.
    QComboBox*    qualityCombo_    = nullptr;
    QComboBox*    languageCombo_   = nullptr;
    QCheckBox*    translateCheck_  = nullptr;

    // Group boxes that can be hidden in "simple" mode.
    QGroupBox*    modeGroup_       = nullptr;
    QGroupBox*    formatsGroup_    = nullptr;
    QCheckBox*    advancedToggle_  = nullptr;

    QSystemTrayIcon* trayIcon_     = nullptr;

    // Animates progressBar_->value() smoothly between updates.
    QPropertyAnimation* progressAnim_ = nullptr;

    // Checks GitHub Releases for a newer version. Fired ~2s after startup.
    UpdateChecker* updateChecker_ = nullptr;

    // Shared with the worker via shared_ptr (capture-by-value in lambda).
    std::shared_ptr<CancellationToken> cancelToken_;

    bool isRunning_ = false;
};

} // namespace subext
