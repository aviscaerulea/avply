#pragma once

#include <QDialog>

// キー・マウス操作の一覧ダイアログ
// 非モーダルで表示し、再生を止めない。閉じる操作は QDialog 既定の経路に任せ、独自の処理を持たない
//（タイトルバーの ×、Esc、Alt+F4 などの Windows 標準の閉じる操作がいずれも reject へ至る）。
// 一覧の内容は README の「キー・マウス操作」節の 2 表（再生・トリム）を写す。マウス列だけは
// docs/index.md と同じく右クリックメニューからの操作も載せる。キー割当を変えたときは
// README.md、README.en.md、docs/index.md、docs/en/index.md と本ダイアログを同時に更新する。
class ShortcutHelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutHelpDialog(QWidget* parent = nullptr);
};
