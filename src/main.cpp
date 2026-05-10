// std::min / std::max と windows.h の min / max マクロが衝突しないよう
// 全 include より前に定義する
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QString>
#include <QTimer>
#include "Config.h"
#include "MainWindow.h"
#include "Settings.h"
#include "SingleInstance.h"

namespace {

// QApplication 構築前にコマンドライン第 1 引数を Unicode 安全に取得する
// argv は MSVCRT が CP932 でナロー化したものなので、CP932 範囲外の文字
// （中国語・絵文字等）が含まれるパスは取得できない。
// GetCommandLineW + CommandLineToArgvW で UTF-16 から直接取得する
QString firstArgumentUnicode()
{
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) return QString();
    QString result;
    if (wargc > 1) result = QString::fromWCharArray(wargv[1]);
    LocalFree(wargv);
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    // 再生速度変更時に pitchCompensation を有効化するため
    // FFmpeg バックエンドを強制する（Media Foundation はピッチ保存非対応）
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");

    // FFmpeg バックエンドの HW デコード優先順位を avply.toml から取得して反映する。
    // QApplication 構築前に qputenv する必要があるため早期ロードする
    // （Config::load() は exe ディレクトリ取得を Win32 API で行うため Qt 初期化非依存）。
    // 空文字なら Qt 自動選択へフォールバックする
    const AppConfig earlyCfg = Config::load();
    if (!earlyCfg.hwDecoderPriority.isEmpty()) {
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES",
                earlyCfg.hwDecoderPriority.toUtf8());
    }

    // 単一インスタンス強制が ON のとき、自身が 2 個目以降なら引数を既存へ転送して即時終了する
    // QApplication 構築前に判定することで、不要な GUI 初期化を避ける
    const bool singleInstanceEnabled = Settings::instance().singleInstance();
    const QString preliminaryArg = firstArgumentUnicode();

    if (singleInstanceEnabled) {
        if (SingleInstance::tryForwardAndExit(preliminaryArg)) return 0;
    }

    // プロセス優先度設定（レジストリ値が ON のときのみ ABOVE_NORMAL）
    if (Settings::instance().aboveNormalPriority()) {
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    }

    QApplication app(argc, argv);

    // ウィンドウアイコンを設定する
    // QRC 経由のアイコンは実行時のタイトルバー・タスクバー・Alt+Tab 表示用。
    // EXE シェル表示（エクスプローラ等）は src/app.rc で別途埋め込み済み。
    // setWindowIcon は QApplication 構築直後に呼び、起動初期のダイアログにも反映させる。
    QIcon appIcon(":/icons/app.ico");
    if (appIcon.isNull()) {
        qWarning("ウィンドウアイコンのロードに失敗しました：:/icons/app.ico");
    }
    app.setWindowIcon(appIcon);

    app.setApplicationName("avply");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("avply");

    // コマンドライン第 1 引数があれば初期ファイルとして MainWindow に渡す
    // （Windows の D&D 起動・「送る」・「プログラムを指定して開く」用）。
    // QApplication 構築前に取得した Unicode 引数をそのまま使う
    // （app.arguments() は Qt 6 でも内部的に WinMain 由来の UTF-16 を再構成するが、
    // 起動経路を一本化するため preliminaryArg を流用する）
    const QString initialPath = preliminaryArg;

    // 起動時の白フラッシュ抑制
    // Windows のネイティブウィンドウ作成直後に発生する WM_ERASEBKGND による白塗りは
    // Qt 側の背景属性では抑止しきれないため、最初の paint が終わるまでウィンドウを
    // 透明化して視覚的に隠す。次のイベントループで不透明に戻すと、その時点では既に
    // VideoView の暗色背景および UI が描画済みのためフラッシュは見えない
    MainWindow win(initialPath);
    win.setWindowOpacity(0.0);
    win.show();
    QTimer::singleShot(0, &win, [&win]() {
        win.setWindowOpacity(1.0);
    });

    // 単一インスタンスが ON のときは primary として IPC サーバを起動し、
    // 後続の起動から送られてくるファイルパスを受信する
    if (singleInstanceEnabled) {
        SingleInstance::startServer(&win, &win);
    }

    return app.exec();
}
