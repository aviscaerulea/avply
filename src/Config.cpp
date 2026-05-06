#include "Config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringConverter>
#include <algorithm>

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
        if (section == "seek" && key == "left_ms")         assignInt(cfg.seekLeftMs);
        if (section == "seek" && key == "right_ms")        assignInt(cfg.seekRightMs);
        if (section == "seek" && key == "wheel_forward_ms") assignInt(cfg.wheelForwardMs);
        if (section == "seek" && key == "wheel_back_ms")    assignInt(cfg.wheelBackMs);

        // 浮動小数点値として解釈する。変換失敗時は既定値を保持する
        auto assignDouble = [&](double& target) {
            bool ok;
            const double v = value.toDouble(&ok);
            if (ok) target = v;
        };
        if (section == "playback" && key == "speed") assignDouble(cfg.playbackSpeed);
        if (section == "window"   && key == "initial_screen_ratio") assignDouble(cfg.initialScreenRatio);
        if (section == "audio"    && key == "volume")               assignDouble(cfg.audioVolume);
    }

    // 再生速度は MainWindow の上下キー操作と同じ範囲（0.05〜4.0）に丸める
    // 0 以下や極端な値が直接 setPlaybackRate に渡らないようにする
    cfg.playbackSpeed = std::clamp(cfg.playbackSpeed, 0.05, 4.0);
    // モニタ比率は 0.1〜1.0 にクランプする
    // 0 以下では初期サイズが破綻し、1.0 超ではタスクバーやマルチモニタ境界を侵す
    cfg.initialScreenRatio = std::clamp(cfg.initialScreenRatio, 0.1, 1.0);
    // 音量は QAudioOutput::setVolume の有効範囲（0.0〜1.0）にクランプ
    cfg.audioVolume = std::clamp(cfg.audioVolume, 0.0, 1.0);
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

    mergeFromFile(exeDir + "/avply.toml",       cfg);
    mergeFromFile(exeDir + "/avply.local.toml", cfg);

    if (cfg.ffmpegPath.isEmpty()) {
        const QString fallback = scoopFallback();
        if (QFile::exists(fallback)) cfg.ffmpegPath = fallback;
    }
    if (cfg.ffmpegPath.isEmpty()) {
        // scoop 以外（chocolatey、winget、手動配置）でも設定不要で動作させる
        const QString resolved = QStandardPaths::findExecutable("ffmpeg");
        if (!resolved.isEmpty()) cfg.ffmpegPath = resolved;
    }
    return cfg;
}
