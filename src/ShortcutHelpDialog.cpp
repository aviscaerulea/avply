#include "ShortcutHelpDialog.h"

#include <QLabel>
#include <QVBoxLayout>

namespace {

// 一覧の本文（HTML）。README の「キー・マウス操作」節の 2 表を写す。
// 表の行順・文言は README と揃える。マウス列の「右クリックメニュー」は docs 側の表記に合わせる
const char* const kShortcutHtml = R"(
<h3>再生の操作</h3>
<table cellspacing="0" cellpadding="4" border="1">
<tr><th>操作</th><th>キー</th><th>マウス</th></tr>
<tr><td>再生 / 停止</td><td>スペース</td><td>プレビュー領域をクリック（音声のみのときは不可）</td></tr>
<tr><td>シーク</td><td>← →</td><td>シークバーのドラッグ、シークバー・プレビュー領域のホイール</td></tr>
<tr><td>大きくシーク</td><td>Shift + ← / Shift + →</td><td></td></tr>
<tr><td>1 フレーム進む / 戻る</td><td>Ctrl + → / Ctrl + ←</td><td></td></tr>
<tr><td>フォルダ内の前 / 次のファイルへ切替</td><td>Alt + ← / Alt + →</td><td></td></tr>
<tr><td>再生速度 ±0.05 倍</td><td>Ctrl + ↑ 速く / Ctrl + ↓ 遅く</td><td>Ctrl + ホイール</td></tr>
<tr><td>音量 ±0.05</td><td>↑ ↓</td><td>Shift + ホイール</td></tr>
<tr><td>音声強調の切替</td><td>C</td><td>右クリックメニュー</td></tr>
<tr><td>字幕の切替</td><td>S</td><td>右クリックメニュー</td></tr>
<tr><td>再生条件の一括リセット</td><td>G</td><td></td></tr>
<tr><td>キー・マウス操作の一覧を表示</td><td>?</td><td>右クリックメニュー</td></tr>
</table>
<h3>トリムの操作</h3>
<table cellspacing="0" cellpadding="4" border="1">
<tr><th>操作</th><th>キー</th><th>マウス</th></tr>
<tr><td>区間の開始位置を指定</td><td>[</td><td>【 ボタン</td></tr>
<tr><td>区間の終了位置を指定</td><td>]</td><td>】 ボタン</td></tr>
<tr><td>区間のみクリア（再生位置は維持）</td><td>R</td><td></td></tr>
<tr><td>トリムの実行 / 中断</td><td></td><td>✂ ボタン</td></tr>
</table>
)";

} // namespace

ShortcutHelpDialog::ShortcutHelpDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("キー・マウス操作");

    auto* label = new QLabel(this);
    label->setTextFormat(Qt::RichText);
    label->setText(kShortcutHtml);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    // 内容が固定のため、レイアウトの推奨サイズで固定する
    layout->setSizeConstraint(QLayout::SetFixedSize);
}
