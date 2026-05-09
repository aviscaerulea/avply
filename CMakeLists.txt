cmake_minimum_required(VERSION 3.25)
project(avply VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets Network Quick)

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
    src/icons.qrc
    src/app.rc
)

target_include_directories(avply PRIVATE src)

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
