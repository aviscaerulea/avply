#include "ModelDownloader.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace {

// 受信が途絶えたと判断するまでの時間（ms）
// 回線断でソケットが閉じない状況を検出する。数百 MB の取得を通すため、全体の制限時間は設けない
constexpr int kTransferTimeoutMs = 30000;

// 未完了ダウンロードの拡張子
// 完了までこの名前で書き、リネームで確定させる。中断・異常終了で壊れた本体を残さない
const QString kPartSuffix = QStringLiteral(".part");

} // namespace

ModelDownloader::ModelDownloader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

ModelDownloader::~ModelDownloader()
{
    cancel();
}

void ModelDownloader::start(const QUrl& url, const QString& destPath)
{
    if (m_reply) return;

    m_destPath = destPath;
    const QString partPath = destPath + kPartSuffix;

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    // 既存の .part があれば続きから取得する
    m_resumeFrom = QFileInfo(partPath).size();
    m_part = new QFile(partPath, this);
    if (!m_part->open(m_resumeFrom > 0 ? QIODevice::Append : QIODevice::WriteOnly)) {
        const QString err = m_part->errorString();
        delete m_part;
        m_part = nullptr;
        emit finished(false, err);
        return;
    }

    QNetworkRequest req(url);
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (m_resumeFrom > 0) {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_resumeFrom) + "-");
    }

    m_reply = m_nam->get(req);

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        // サーバが Range を無視して全体を返したら（206 以外）先頭から書き直す。
        // 最初の受信時点で判定し、以後は追記を続ける
        if (m_resumeFrom > 0) {
            const int status = m_reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 206) {
                m_part->seek(0);
                m_part->resize(0);
                m_resumeFrom = 0;
            }
        }
        m_part->write(m_reply->readAll());
    });

    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        emit progress(m_resumeFrom + received, total > 0 ? m_resumeFrom + total : -1);
    });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply->error() != QNetworkReply::NoError) {
            fail(m_reply->errorString());
            return;
        }
        m_part->write(m_reply->readAll());
        const QString partPath = m_part->fileName();
        m_part->close();
        cleanup();

        // 既存の本体があれば置き換える。rename は上書きしないため先に消す
        QFile::remove(m_destPath);
        if (!QFile::rename(partPath, m_destPath)) {
            qWarning() << "ModelDownloader: リネームに失敗:" << partPath << "->" << m_destPath;
            emit finished(false, QStringLiteral("ダウンロードしたファイルを配置できません"));
            return;
        }
        emit finished(true, QString());
    });
}

void ModelDownloader::cancel()
{
    if (!m_reply) return;
    // abort は finished を発火させるが、reply を先に切り離して完了処理へ入らせない
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();

    if (m_part) {
        m_part->close();
        delete m_part;
        m_part = nullptr;
    }
}

void ModelDownloader::fail(const QString& error)
{
    qWarning() << "ModelDownloader: ダウンロードに失敗:" << error;
    if (m_part) m_part->close();
    cleanup();
    emit finished(false, error);
}

void ModelDownloader::cleanup()
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_part) {
        delete m_part;
        m_part = nullptr;
    }
}
