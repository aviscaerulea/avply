#include "Config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringConverter>

namespace {

// セクションヘッダ行か判定し、セクション名を抽出する
// [section] 形式に一致した場合のみ true を返す
bool parseSection(const QString& line, QString& section)
{
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith('[') || !trimmed.endsWith(']')) return false;
    section = trimmed.mid(1, trimmed.size() - 2).trimmed();
    return !section.isEmpty();
}

// key = "value" 形式の 1 行を解釈する
// コメント（# 始まり）と空行は無視。値はダブルクォートを除去
bool parseKeyValue(const QString& line, QString& key, QString& value)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#')) return false;

    const int eq = trimmed.indexOf('=');
    if (eq <= 0) return false;

    key = trimmed.left(eq).trimmed();
    QString raw = trimmed.mid(eq + 1).trimmed();
    if (raw.size() >= 2 && raw.startsWith('"') && raw.endsWith('"')) {
        raw = raw.mid(1, raw.size() - 2);
    }
    value = raw;
    return !key.isEmpty();
}

// TOML ファイルを読み込み、cfg をセクション単位で上書きする
// ファイル不存在時は何もしない
void mergeFromFile(const QString& path, AppConfig& cfg)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    QString section;
    while (!in.atEnd()) {
        const QString line = in.readLine();

        QString sectionName;
        if (parseSection(line, sectionName)) {
            section = sectionName;
            continue;
        }

        QString key, value;
        if (!parseKeyValue(line, key, value)) continue;

        if (section == "ffmpeg" && key == "path") cfg.ffmpegPath = value;

        // 整数値として解釈する。変換失敗時は既定値を保持する
        auto assignInt = [&](int& target) {
            bool ok;
            const int v = value.toInt(&ok);
            if (ok) target = v;
        };
        if (section == "seek" && key == "left_ms")  assignInt(cfg.seekLeftMs);
        if (section == "seek" && key == "right_ms") assignInt(cfg.seekRightMs);
    }
}

// scoop デフォルトの ffmpeg.exe パスを返す
QString scoopFallback()
{
    return QDir::homePath() + "/scoop/apps/ffmpeg/current/bin/ffmpeg.exe";
}

} // namespace

AppConfig Config::load()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    AppConfig cfg;

    mergeFromFile(exeDir + "/vcutter.toml",       cfg);
    mergeFromFile(exeDir + "/vcutter.local.toml", cfg);

    if (cfg.ffmpegPath.isEmpty()) {
        const QString fallback = scoopFallback();
        if (QFile::exists(fallback)) cfg.ffmpegPath = fallback;
    }
    return cfg;
}
