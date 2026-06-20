# avply ユニットテスト
# - 各 testbin は必要最小限のソースのみリンクする（OBJECT library 化は YAGNI のため見送り）
# - windeployqt は打たず、ctest 実行時に Qt の bin/ を PATH 先頭へ注入する
# - Q_OBJECT を含むテストクラスは親 CMakeLists の AUTOMOC で moc される

find_package(Qt6 REQUIRED COMPONENTS Test)

set(AVPLY_SRC ${CMAKE_SOURCE_DIR}/src)

# Qt の bin/ ディレクトリ
# 親 CMakeLists の windeployqt 参照と同様、Qt6_DIR の 3 階層上の bin を使う
set(QT_BIN "${Qt6_DIR}/../../../bin")

# avply 用テスト追加ラッパ
# 共通リンク・include・ctest 登録を 1 箇所に集約する。WIDGETS オプションを付けると
# Qt6::Gui / Qt6::Widgets を追加リンクする。残りの引数はソースファイルとして扱う
function(avply_add_test name)
    set(opts WIDGETS)
    cmake_parse_arguments(ARG "${opts}" "" "" ${ARGN})

    add_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
    # ctest 実行時に stdout を捕捉するため CONSOLE サブシステムで生成する
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(${name} PRIVATE ${AVPLY_SRC})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
    if(ARG_WIDGETS)
        target_link_libraries(${name} PRIVATE Qt6::Gui Qt6::Widgets)
    endif()
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()

    add_test(NAME ${name} COMMAND ${name})
    # Qt DLL を実行時に解決するため bin/ を PATH 先頭へ追加する
    # ヘッドレス環境で QApplication が platform plugin を要求するため offscreen も併せて指定する
    # CMake 3.22+ の ENVIRONMENT_MODIFICATION で path_list_prepend / set を使う
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${QT_BIN};QT_QPA_PLATFORM=set:offscreen")
endfunction()

# Phase 1 テスト一式
avply_add_test(test_OutputNamer
    test_OutputNamer.cpp
    ${AVPLY_SRC}/OutputNamer.cpp
)

avply_add_test(test_Settings
    test_Settings.cpp
    ${AVPLY_SRC}/Settings.cpp
)

avply_add_test(test_FfmpegRunner_path
    test_FfmpegRunner_path.cpp
    ${AVPLY_SRC}/FfmpegRunner.cpp
)

avply_add_test(test_RangeSlider WIDGETS
    test_RangeSlider.cpp
    ${AVPLY_SRC}/RangeSlider.cpp
)

avply_add_test(test_SeekPreview WIDGETS
    test_SeekPreview.cpp
    ${AVPLY_SRC}/SeekPreview.cpp
)
