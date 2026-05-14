#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace subext {

// =======================================================================
// UPDATE CHECKER
// =======================================================================
// Queries the GitHub Releases API for the project's latest release tag,
// compares to the compiled-in APP_VERSION, and emits a signal if an
// update is available.
//
// Design notes:
//   - Asynchronous: checkForUpdate() returns immediately. Results arrive
//     via signals so the GUI thread is never blocked on the network.
//   - Failure modes are silent: emit checkFailed() and let the caller
//     decide whether to surface anything. We do NOT want every Wi-Fi
//     glitch to interrupt the user with a popup.
//   - "Open URL in browser" is the v1 update flow. Auto-download +
//     auto-run is more polished but has a wider attack surface — that's
//     a v2 decision worth a proper security review.
//
// INTERVIEW NOTE: this is the OBSERVER PATTERN once more (signals/slots),
// AND a classic example of asynchronous I/O in Qt. "Why use signals
// instead of a synchronous return value?" → networking takes seconds;
// blocking the UI for that is unacceptable; callbacks are the only
// reasonable shape.
// =======================================================================
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    // Fire-and-forget. Results arrive on the signals below.
    void checkForUpdate();

signals:
    void updateAvailable(const QString& latestVersion,
                         const QString& downloadUrl);
    void noUpdateAvailable(const QString& currentVersion);
    void checkFailed(const QString& errorMessage);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* nam_ = nullptr;
};

} // namespace subext
