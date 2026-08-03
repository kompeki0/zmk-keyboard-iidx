# zmk-keyboard-iidx

ZMK用のIIDXコントローラー設定と、IIDX向けHID実装を同梱するリポジトリです。
`zmk-iidx`のUSB HID、mode/button/boot-select behavior、およびオプションの
vendor BLE serviceをこのZephyrモジュール内に統合しています。

## IIDX transport

- USB: report IDなしの5-byte joystick reportを`HID_1`から送信
- BLE（オプション）: `0xFF00` vendor serviceから送信

BLE transportを使用する場合はshield設定に次を追加します。

```conf
CONFIG_ZMK_IIDX_BLE=y
```

USB HID接続中は通常USBを使用しますが、preferred outputをBLEへ切り替えると、
USB給電・接続中でもBLEのnotifyを継続します。未指定時は従来どおりUSB IIDX HIDのみが有効です。

BLEデバイス名は16文字の`IIDX Entry model`です。Zephyrの終端文字を含むバッファ要件に
合わせて`CONFIG_BT_DEVICE_NAME_MAX=17`を指定しています。

vendor serviceには互換性のため次のcharacteristicを用意しています。

- `0xFF01`: 2つの5-byte input recordを連結した10-byte notify
- `0xFF02`: write / write without response
- `0xFF03`: notify / write。notify購読開始時に`01 05`を送信

advertising packetにはbeatble互換のApple iBeacon manufacturer dataを設定し、scan
responseには`0xFF00`のComplete 16-bit Service UUIDと`IIDX Entry model`を設定します。
`0xFF01`の10-byte input packetは8ms周期（約125回/秒）で通知します。
sequenceは`01/02`から始まり、通知ごとに2増加します。現在使用するoption buttonは
E1/E2ですが、送信形式と内部HID reportは将来のE3/E4にも対応しています。

USBのHID poll intervalは4msなので、入力からUSB転送までの待ちは通常0～4msです。
BLEは8msの通知周期と要求値7.5msのconnection intervalがあるため、ファームウェアから
無線送信までの待ちは概ね0～15.5ms（平均約8ms）です。central側が別のconnection
intervalを選択した場合や無線再送、OS・アプリの処理、画面描画によって実測値は増えます。

BLEの有効化そのものはkeymap layerを変更しません。通常起動時は先頭の`bms_layer`が
有効で、皿はE1/E2として動作します。起動後1秒以内に第1ボタンを押して
`iidx_layer`へ移動した場合、皿はX軸値として動作します。

## Bluetooth settings

電源投入後1秒以内に第2ボタンを押すとBluetooth設定レイヤへ移動します。通常起動後の
第2ボタンは従来どおりIIDX button 2として動作します。設定レイヤでは第1ボタンを押すと
`BT_CLR_ALL`を実行し、保存されている全Bluetooth profileのbondを削除します。第2ボタンを
押すとpreferred outputをBLEへ強制変更し、`iidx_layer`（layer 1）へ移動します。

## Scratch sensitivity settings

Bluetooth設定レイヤの第3ボタンを押すと、皿感度設定レイヤ（layer 3）へ移動します。
フォーカス中のテキストエディタへ通常のキーボード入力として設定画面を表示するため、
あらかじめ空のエディタを開いてください。

- 第1ボタン: 感度を1下げる
- 第2ボタン: 感度を1上げる
- 第3ボタン: 初期値5へ戻す
- 第4ボタン: 設定画面を再表示する
- 第5ボタン: 設定画面を終了して`iidx_layer`へ移動する

感度は1～20で、エンコーダ1 tickあたりのX軸変化量です。回転方向は従来のまま維持され、
変更は直ちに反映されます。値はZephyr settingsへ保存され、電源を切っても維持されます。

## Repository layout

- `boards/shields/iidx`: shield、keymap、firmware設定
- `src`: IIDX HIDとbehaviorの実装
- `include/zmk_iidx`: IIDX実装の公開API
- `dts/bindings/behaviors`: 独自behaviorのDevicetree binding

ZMK本体と汎用encoder behaviorなどの依存モジュールは`config/west.yml`から取得しますが、
IIDX HID本体について別の`zmk-iidx` checkoutは必要ありません。
