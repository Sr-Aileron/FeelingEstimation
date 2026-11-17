# FeelingEstimation

Arduino Uno に心拍センサー（MAX30102）と加速度センサー（MPU-6050）をつなぎ、
「心拍の高低 × 動きの多少」で4曲から1曲を選んで DFPlayer Mini でループ再生する装置。
OLED（SSD1306）に曲名と心拍数を表示する。

## ディレクトリ

- `src/main/` — 本体スケッチ（判定・再生・表示）
- `src/{i2c_scanner,oled,beat_sensor,acceleration_sensor,dfplayer}/` — 部品ごとの動作確認用スケッチ（組み立て順）
- `doc/hardware.pdf` — 配線ガイド ver1（部品表・配線・トラブルシューティング）
- `doc/software.pdf` — ソフトウェアガイド ver2（仕様・各スケッチの全文・調整方法）

PDF は重いので、まずこのファイルを参照し、詳細が必要なときだけ読むこと。

## コマンド

テストフレームワークは無い。**スケッチを変更したら必ず `make compile` で検証する**（RAM 使用量も確認）。

```bash
make compile SKETCH=main     # ビルド（既定 SKETCH=main）
make compile-all             # 全スケッチ
make upload SKETCH=main      # 書き込み（実機が必要。勝手に実行しない）
make capture DURATION=30     # シリアル出力を logs/ に保存（実機が必要）
make ports                   # 接続ボード一覧
make setup                   # コア・ライブラリを固定バージョンで導入
```

arduino-cli は PATH に無ければ Arduino IDE 同梱版を使う。

## ハードウェア制約（重要）

- **RAM 2KB**。main は現在グローバル変数 1139B（55%）。U8x8（文字モード）を使っているのはメモリ節約のため。
  - フルバッファの U8g2 / Adafruit SSD1306 / Adafruit MPU6050 は使わない
  - 文字列定数を増やすなら `F("...")` を使う。日本語表示は不可（フォント容量）
  - 変更後は compile 出力の RAM 使用率を報告する。75% を超えたら要注意
- ピン割り当て: I2C = A4(SDA)/A5(SCL)、DFPlayer = D10(RX←DFPlayer TX)/D11(TX→1kΩ→DFPlayer RX)、D0/D1 は USB 用に空ける。D2/D3/D4 は任意（MAX30102 INT / MPU INT / DFPlayer BUSY）、現状未使用
- I2C アドレス: OLED 0x3C（まれに 0x3D）、MAX30102 0x57、MPU-6050 0x68
- MPU-6050 はライブラリを使わず Wire で直接読む（±2g、16384 LSB/g）
- シリアル: 115200bps（i2c_scanner のみ 9600bps）
- microSD: ルートに `0001.mp3`〜`0004.mp3`

## 仕様（main）

| | 動き少（< 2.0 m/s²） | 動き多（≥ 2.0） |
|---|---|---|
| 心拍低（< 90 BPM） | track1 | track2 |
| 心拍高（≥ 90 BPM） | track3 | track4 |

- 心拍: 直近4拍の BPM 平均。指を離すとリセット、4拍揃うまで `beatAvg == 0`
- 動き: |加速度の大きさ − 9.80665| を 50ms ごとに積算し、判定ごとに平均
- 10秒ごとに判定。区分が変わったときだけ `player.loop(track)`。指なし/測定中は現状維持
- `readHeart()` は loop 毎回呼ぶ（拍の取りこぼし防止）。`delay()` を main の loop に入れない
- 調整値は `main.ino` 冒頭の「調整用の設定値」に集約する

## 規約

- コメントは日本語、既存のスタイル（2スペース、`// =====` 区切り）に合わせる
- 動作確認用スケッチは独立して書き込めるよう自己完結させる（共通ヘッダ化しない）
- `doc/software.pdf` にはスケッチ全文が載っている。スケッチを変えたらドキュメントとの差分が出ることをユーザーに伝える
- 新しいスケッチは `src/<name>/<name>.ino` に置く（Arduino の規則：フォルダ名＝ファイル名）
