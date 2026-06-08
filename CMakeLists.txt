cmake_minimum_required(VERSION 3.25)
project(avply VERSION 1.0.0 LANGUAGES CXX)

# アプリ表示バージョン。未指定時は CMakeLists の PROJECT_VERSION を使用する
# リリースビルド時は GHA ワークフローが -DAVPLY_VERSION=<git-tag> で上書きする
set(AVPLY_VERSION "${PROJECT_VERSION}" CACHE STRING "アプリ表示バージョン（リリース時は git タグで上書き）")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets Network Quick)

# SoundTouch: WSOLA による pitch 保持の時間圧縮 / 伸長ライブラリ（LGPL）
# QAudioBufferOutput が pitchCompensation を無視するため、AudioWorker 側で
# playback rate に応じた time-stretch を行うために導入する
include(FetchContent)
set(SOUNDTOUCH_DLL OFF CACHE BOOL "" FORCE)
set(INTEGER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SOUNDSTRETCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SoundTouch
    GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
    GIT_TAG 2.4.0
)
FetchContent_MakeAvailable(SoundTouch)

# WebRTC Audio Processing (APM): AGC2 + ノイズ抑制 + ハイパスフィルタ（BSD）
# vcpkg にポートが無いため meson + ninja で事前ビルドした静的 lib とヘッダを同梱して参照する。
# ベンダ配置は third_party/webrtc-audio-processing 配下とし、WEBRTC_APM_ROOT で上書き可能。
#   include/  … 公開ヘッダ（webrtc/... と api/... のツリー）
#   lib/      … 静的 lib 群（webrtc-audio-processing 本体と abseil 依存を含む全 .lib）
# meson は abseil をサブプロジェクトとして個別 lib に分けて出力するため、lib/ の全 .lib を一括リンクする
set(WEBRTC_APM_ROOT "${CMAKE_SOURCE_DIR}/third_party/webrtc-audio-processing"
    CACHE PATH "Prebuilt webrtc-audio-processing root (include/ and lib/)")
file(GLOB WEBRTC_APM_LIBS "${WEBRTC_APM_ROOT}/lib/*.lib")
if(NOT WEBRTC_APM_LIBS)
    message(FATAL_ERROR
        "webrtc-audio-processing の静的 lib が見つからない: ${WEBRTC_APM_ROOT}/lib/*.lib"
        " — meson 事前ビルドの成果物を同梱するか WEBRTC_APM_ROOT を指定すること")
endif()

qt_add_executable(avply WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/FfmpegRunner.h
    src/FfmpegRunner.cpp
    src/VideoView.h
    src/VideoView.cpp
    src/RangeSlider.h
    src/RangeSlider.cpp
    src/SeekPreview.h
    src/SeekPreview.cpp
    src/ThumbnailExtractor.h
    src/ThumbnailExtractor.cpp
    src/Encoder.h
    src/Encoder.cpp
    src/OutputNamer.h
    src/OutputNamer.cpp
    src/Config.h
    src/Config.cpp
    src/Settings.h
    src/Settings.cpp
    src/SingleInstance.h
    src/SingleInstance.cpp
    src/SilenceTone.h
    src/SilenceTone.cpp
    src/SpeechEnhancer.h
    src/SpeechEnhancer.cpp
    src/AudioWorker.h
    src/AudioWorker.cpp
    src/icons.qrc
    src/app.rc
)

# AVPLY_VERSION をバイナリへ文字列マクロとして注入する
# main.cpp の setApplicationVersion で参照し、コンテキストメニューの About 項目に表示する
target_compile_definitions(avply PRIVATE AVPLY_VERSION="${AVPLY_VERSION}")

# include/        … <webrtc/...> と <absl/...> の解決用
# include/webrtc/ … webrtc 公開ヘッダ内部の "api/..." "rtc_base/..." 相対 include 解決用
target_include_directories(avply PRIVATE src
    "${WEBRTC_APM_ROOT}/include"
    "${WEBRTC_APM_ROOT}/include/webrtc")

# BOM なし UTF-8 ソースを正しくコンパイルする（MSVC デフォルトは CP932）
if(MSVC)
    target_compile_options(avply PRIVATE /utf-8)
    # rc.exe を UTF-8 codepage で実行する（VERSIONINFO 等で日本語を含めても文字化けしないため）
    target_compile_options(avply PRIVATE $<$<COMPILE_LANGUAGE:RC>:/c65001>)
endif()

target_link_libraries(avply PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Network
    Qt6::Quick
    SoundTouch
    ${WEBRTC_APM_LIBS}
    winmm    # webrtc rtc_base の timeGetTime（SystemTimeNanos）が依存する
    dbghelp  # クラッシュ時の MiniDumpWriteDump（main.cpp の未処理例外フィルタ）が依存する
)

# Qt DLL を実行ファイルと同じディレクトリに配置する
if(WIN32)
    add_custom_command(TARGET avply POST_BUILD
        COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe"
                --qmldir "${CMAKE_SOURCE_DIR}/src"
                "$<TARGET_FILE:avply>"
        COMMENT "Qt DLL を配置しています..."
    )
endif()

# avply.toml の出力ディレクトリへの配置
# avply.local.toml は VCS 管理外の個人環境固有設定のためコピーしない
add_custom_command(TARGET avply POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/avply.toml"
            "$<TARGET_FILE_DIR:avply>/avply.toml"
    COMMENT "avply.toml を配置中..."
)
