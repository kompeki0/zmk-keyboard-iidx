# zmk-keyboard-iidx

ZMK用のIIDXコントローラー設定と、IIDX向けHID実装を同梱するリポジトリです。
`zmk-iidx`のUSB HID、mode/button/boot-select behavior、およびオプションの
vendor BLE serviceをこのZephyrモジュール内に統合しています。

## IIDX transport

- USB: report IDなしの5-byte joystick reportを`HID_1`から送信
- BLE（オプション）: `0xFF00` service / `0xFF01` notify characteristicから送信

BLE transportを使用する場合はshield設定に次を追加します。

```conf
CONFIG_ZMK_IIDX_BLE=y
```

BLEはUSB HID接続中にはnotifyを停止し、USBを優先します。未指定時は従来どおり
USB IIDX HIDのみが有効です。

BLEデバイス名は16文字の`IIDX Entry model`です。Zephyrの終端文字を含むバッファ要件に
合わせて`CONFIG_BT_DEVICE_NAME_MAX=17`を指定しています。

## Bluetooth settings

電源投入後1秒以内に第2ボタンを押すとBluetooth設定レイヤへ移動します。通常起動後の
第2ボタンは従来どおりIIDX button 2として動作します。設定レイヤでは第1ボタンを押すと
`BT_CLR_ALL`を実行し、保存されている全Bluetooth profileのbondを削除します。操作後は
電源を入れ直して通常レイヤへ戻します。

## Repository layout

- `boards/shields/iidx`: shield、keymap、firmware設定
- `src`: IIDX HIDとbehaviorの実装
- `include/zmk_iidx`: IIDX実装の公開API
- `dts/bindings/behaviors`: 独自behaviorのDevicetree binding

ZMK本体と汎用encoder behaviorなどの依存モジュールは`config/west.yml`から取得しますが、
IIDX HID本体について別の`zmk-iidx` checkoutは必要ありません。
