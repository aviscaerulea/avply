#pragma once
#include <QString>

// 出力ファイルパス生成ユーティリティ
// 形式: {inputDir}/{baseName}_mod.{outputExt}
// 重複時: {baseName}_mod2.{outputExt}, {baseName}_mod3.{outputExt} ...
// baseName 末尾が既に _mod / _mod<数字> の場合は剥がして付け直し、_mod_mod を防ぐ
class OutputNamer {
public:
    // 入力ファイルパスと出力拡張子（先頭の "." なし）から出力パスを生成する
    static QString generate(const QString& inputPath, const QString& outputExt);
};
