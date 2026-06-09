#pragma once
#include <QString>

// 出力ファイルパス生成ユーティリティ
// 形式: {inputDir}/{baseName}_mod.{outputExt}
// 重複時: {baseName}_mod2.{outputExt}, {baseName}_mod3.{outputExt} ...
// baseName 末尾が既に _mod / _mod<数字> の場合は同名へ上書き出力する（再編集の成果物は置き換える）
class OutputNamer {
public:
    // 入力ファイルパスと出力拡張子（先頭の "." なし）から出力パスを生成する
    static QString generate(const QString& inputPath, const QString& outputExt);

    // ベース名が _mod / _mod<数字> 形式かを判定する
    // true のとき generate は同名パスを返し、出力先への上書きが許可される
    static bool isModName(const QString& inputPath);
};
