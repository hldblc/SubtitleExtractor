#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <memory>

class QLineEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QTextEdit;
class QProgressBar;
class QLabel;
class QListWidget;
class QPropertyAnimation;

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
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onClearQueue();
    void onBrowseModel();
    void onBrowseOutput();
    void onExtractOrCancelClicked();
    void onModeChanged(int index);
    void appendLog(const QString& msg);
    void setProgress(int percent);
    void setStatus(const QString& text);
    void onBatchFinished(int successCount, int failCount, bool cancelled);
    void onUpdateAvailable(const QString& latestVersion, const QString& downloadUrl);

private:
    void setupUi();
    void connectSignals();
    void styleProgressBar();
    void setRunningState(bool running);
    void runBatchOnWorker(const BatchRequest& req,
                          std::shared_ptr<CancellationToken> cancel);

    static QString findDefaultModelFile();

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

    // Animates progressBar_->value() smoothly between updates.
    QPropertyAnimation* progressAnim_ = nullptr;

    // Checks GitHub Releases for a newer version. Fired ~2s after startup.
    UpdateChecker* updateChecker_ = nullptr;

    // Shared with the worker via shared_ptr (capture-by-value in lambda).
    std::shared_ptr<CancellationToken> cancelToken_;

    bool isRunning_ = false;
};

} // namespace subext
