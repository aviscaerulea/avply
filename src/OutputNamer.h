#pragma once
#include <QString>

// 出力ファイルパス生成ユーティリティ
// 形式: {inputDir}/{baseName}_cut.mp4
// 重複時: {baseName}_cut_2.mp4, {baseName}_cut_3.mp4 ...
class OutputNamer {
public:
    // 入力ファイルパスから出力ファイルパスを生成する
    static QString generate(const QString& inputPath);
};
