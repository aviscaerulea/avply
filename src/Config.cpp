#include "Config.h"
#define NOMINMAX
#include <windows.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringConverter>
#include <algorithm>
#include <vector>

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
        if (section == "audio"    && key == "silence_tone_freq_hz") assignDouble(cfg.silenceToneFreqHz);
        if (section == "audio"    && key == "silence_tone_amp")     assignDouble(cfg.silenceToneAmp);

        // 真偽値（true / false / 1 / 0 を受理。それ以外は無視）
        auto assignBool = [&](bool& target) {
            const QString v = value.trimmed().toLower();
            if (v == "true" || v == "1") {
                target = true;
            }
            else if (v == "false" || v == "0") {
                target = false;
            }
        };
        if (section == "audio" && key == "silence_tone_enabled") assignBool(cfg.silenceToneEnabled);

        // 文字列値（[playback] の HW デコード関連）
        // 空文字を明示すれば Qt 自動選択 / -hwaccel 指定スキップへフォールバックできる
        if (section == "playback" && key == "hw_decoder_priority") cfg.hwDecoderPriority = value;
        if (section == "playback" && key == "thumbnail_hwaccel")   cfg.thumbnailHwaccel  = value;
    }
}

// 設定値を安全範囲にクランプする
// 全 toml ファイル読込後に一度だけ呼ぶ。範囲外の入力が QMediaPlayer や QAudioOutput に渡るのを防ぐ
void clampConfig(AppConfig& cfg)
{
    // 再生速度は MainWindow の上下キー操作と同じ範囲（0.05〜4.0）に丸める
    cfg.playbackSpeed = std::clamp(cfg.playbackSpeed, 0.05, 4.0);
    // モニタ比率は 0.1〜1.0 にクランプ（0 以下で初期サイズ破綻、1.0 超でタスクバー侵入）
    cfg.initialScreenRatio = std::clamp(cfg.initialScreenRatio, 0.1, 1.0);
    // 音量は QAudioOutput::setVolume の有効範囲（0.0〜1.0）にクランプ
    cfg.audioVolume = std::clamp(cfg.audioVolume, 0.0, 1.0);
    // サイレンストーン周波数は 20〜20000 Hz、振幅は 0.0〜0.01 にクランプ
    // 振幅 0.01（-40 dB）は明確に可聴で常用には不適。設定ミス時の保険として上限を低く取る
    cfg.silenceToneFreqHz = std::clamp(cfg.silenceToneFreqHz, 20.0, 20000.0);
    cfg.silenceToneAmp    = std::clamp(cfg.silenceToneAmp, 0.0, 0.01);
}

// scoop デフォルトの ffmpeg.exe パスを返す
QString scoopFallback()
{
    return QDir::homePath() + "/scoop/apps/ffmpeg/current/bin/ffmpeg.exe";
}

// 実行ファイルのあるディレクトリを返す
// QCoreApplication が未構築でも動くよう Win32 API を直接使う。
// main.cpp の qputenv 経路から QApplication 構築前に呼ばれる。
// ロングパス（MAX_PATH 超）に対応するためバッファを動的拡張する
QString exeDirectory()
{
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    while (n != 0 && n >= buf.size()) {
        buf.resize(buf.size() * 2);
        n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    }
    if (n == 0) return QString();

    QString path = QString::fromWCharArray(buf.data(), static_cast<int>(n));
    const int slash = path.lastIndexOf('/');
    const int back  = path.lastIndexOf('\\');
    const int sep   = std::max(slash, back);
    return (sep >= 0) ? path.left(sep) : QString();
}

} // namespace

AppConfig Config::load()
{
    const QString exeDir = exeDirectory();
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

    clampConfig(cfg);
    return cfg;
}
