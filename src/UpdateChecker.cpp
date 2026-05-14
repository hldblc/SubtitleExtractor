#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace subext {

// =======================================================================
//  CONFIGURE THESE FOR YOUR GITHUB REPO  ←—— halit, change these lines
// =======================================================================
// After you create the repo on GitHub:
//   1. Replace kGitHubOwner with your GitHub username.
//   2. Replace kGitHubRepo with the repo name (e.g. "subtitle-extractor").
//   3. Make sure your installer's release filename starts with kAssetPrefix,
//      otherwise the asset lookup below won't find it.
// =======================================================================
namespace {

constexpr const char* kGitHubOwner = "hldblc";
constexpr const char* kGitHubRepo  = "SubtitleExtractor";

// Asset selector — matches the installer.iss OutputBaseFilename prefix.
// We want only the .exe asset, not source-code zips that GitHub bundles.
constexpr const char* kAssetPrefix = "SubtitleExtractor_Setup_";
constexpr const char* kAssetSuffix = ".exe";

// Built into the binary via target_compile_definitions in CMakeLists.txt.
QString currentVersionString() {
#ifdef APP_VERSION
    return QString::fromLatin1(APP_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

// Strip a leading 'v' if present: "v1.2.3" -> "1.2.3".
// GitHub release tags conventionally use the 'v' prefix.
QString stripLeadingV(const QString& s) {
    return (!s.isEmpty() && s.front() == QChar('v')) ? s.mid(1) : s;
}

// Compare semver-ish version strings numerically.
// Returns > 0 if a is newer than b, 0 if equal, < 0 if older.
//
// DSA NOTE: split on '.', compare integer-wise component-wise. Missing
// trailing components are treated as 0 ("1.2" == "1.2.0"). This is the
// same algorithm npm and pip use for their bare-bones semver compare —
// the full SemVer spec adds pre-release identifiers like "1.2.0-rc.1"
// which we deliberately ignore for simplicity.
int compareVersions(const QString& a, const QString& b) {
    const QStringList pa = a.split('.');
    const QStringList pb = b.split('.');
    const int n = std::max(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = (i < pa.size()) ? pa[i].toInt() : 0;
        const int y = (i < pb.size()) ? pb[i].toInt() : 0;
        if (x != y) return x - y;
    }
    return 0;
}

} // anonymous namespace


UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent),
      nam_(new QNetworkAccessManager(this)) {
    connect(nam_, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::checkForUpdate() {
    const QUrl url(QString("https://api.github.com/repos/%1/%2/releases/latest")
                       .arg(kGitHubOwner, kGitHubRepo));
    QNetworkRequest req(url);
    // GitHub recommends sending a User-Agent identifying the client.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "SubtitleExtractor-UpdateChecker");
    req.setRawHeader("Accept", "application/vnd.github+json");
    nam_->get(req);
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply) {
    // Schedule cleanup once we return — never delete reply directly,
    // QNetworkAccessManager may still hold internal references.
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(QStringLiteral("Network error: ") + reply->errorString());
        return;
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        emit checkFailed(QStringLiteral("Malformed JSON from GitHub: ") +
                         pe.errorString());
        return;
    }

    const QJsonObject release = doc.object();
    const QString tag = stripLeadingV(release.value("tag_name").toString());
    if (tag.isEmpty()) {
        emit checkFailed(QStringLiteral("Release JSON has no tag_name."));
        return;
    }

    // Walk the assets array and pick the .exe matching our prefix.
    // GitHub also auto-attaches Source-code .zip and .tar.gz — skip those.
    QString downloadUrl;
    const QJsonArray assets = release.value("assets").toArray();
    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value("name").toString();
        if (name.startsWith(kAssetPrefix) && name.endsWith(kAssetSuffix)) {
            downloadUrl = a.value("browser_download_url").toString();
            break;
        }
    }

    const QString current = currentVersionString();
    if (compareVersions(tag, current) > 0) {
        if (downloadUrl.isEmpty()) {
            emit checkFailed(
                QStringLiteral("Newer version ") + tag +
                QStringLiteral(" exists but no installer asset was found "
                               "on the release."));
            return;
        }
        emit updateAvailable(tag, downloadUrl);
    } else {
        emit noUpdateAvailable(current);
    }
}

} // namespace subext
