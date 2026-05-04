#pragma once
#include <QString>

// 出力ファイルパス生成ユーティリティ
// 形式: {inputDir}/{baseName}_clip.{outputExt}
// 重複時: {baseName}_clip_2.{outputExt}, {baseName}_clip_3.{outputExt} ...
class OutputNamer {
public:
    // 入力ファイルパスと出力拡張子（先頭の "." なし）から出力パスを生成する
    static QString generate(const QString& inputPath, const QString& outputExt);
};
