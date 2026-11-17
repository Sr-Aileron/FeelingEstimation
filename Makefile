# FeelingEstimation — arduino-cli ラッパー
#
#   make compile [SKETCH=main]          スケッチをビルド（メモリ使用量も表示）
#   make compile-all                    src/ 以下の全スケッチをビルド
#   make upload  [SKETCH=main] [PORT=]  ビルドして Uno に書き込み
#   make monitor [SKETCH=main]          シリアルモニタ（Ctrl+C で終了）
#   make capture [SKETCH=main] [DURATION=30]  シリアル出力を一定時間 logs/ に保存
#   make ports                          接続中のボード一覧
#   make setup                          必要なコア・ライブラリを固定バージョンで導入

# PATH に arduino-cli が無ければ Arduino IDE 同梱版を使う
ARDUINO_CLI ?= $(shell command -v arduino-cli 2>/dev/null || echo "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")
CLI := "$(ARDUINO_CLI)"

FQBN    ?= arduino:avr:uno
SKETCH  ?= main
SKETCHES := $(notdir $(patsubst %/,%,$(dir $(wildcard src/*/*.ino))))
PORT    ?= $(firstword $(wildcard /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial*))
# I2Cスキャナだけ 9600bps、他は 115200bps
BAUD    ?= $(if $(filter i2c_scanner,$(SKETCH)),9600,115200)
DURATION ?= 30

# ライブラリは動作確認済みのバージョンに固定
CORE := arduino:avr@1.8.8
LIBS := "U8g2@2.36.19" \
        "DFRobotDFPlayerMini@1.0.6" \
        "SparkFun MAX3010x Pulse and Proximity Sensor Library@1.1.2"

.PHONY: compile compile-all upload monitor capture ports setup clean check-port list

compile:
	@test -f src/$(SKETCH)/$(SKETCH).ino || { echo "src/$(SKETCH)/$(SKETCH).ino がありません。候補: $(SKETCHES)"; exit 1; }
	$(CLI) compile -b $(FQBN) --build-path build/$(SKETCH) --warnings default src/$(SKETCH)

compile-all:
	@set -e; for s in $(SKETCHES); do echo "== $$s"; $(MAKE) --no-print-directory compile SKETCH=$$s; done

check-port:
	@test -n "$(PORT)" || { echo "Uno が見つかりません。USB を接続するか PORT=/dev/cu.xxx を指定してください"; $(CLI) board list; exit 1; }

upload: compile check-port
	$(CLI) upload -b $(FQBN) -p $(PORT) --input-dir build/$(SKETCH) src/$(SKETCH)

monitor: check-port
	$(CLI) monitor -p $(PORT) -c baudrate=$(BAUD)

capture: check-port
	@mkdir -p logs
	@f=logs/$(SKETCH)-$$(date +%Y%m%d-%H%M%S).log; \
	echo "$(DURATION)秒間 $(PORT) @ $(BAUD)bps を記録 → $$f"; \
	perl -e 'alarm shift; exec @ARGV' $(DURATION) $(CLI) monitor -p $(PORT) -c baudrate=$(BAUD) --quiet > $$f || true; \
	echo "--- 末尾"; tail -n 20 $$f

ports:
	$(CLI) board list

list:
	@echo $(SKETCHES)

setup:
	$(CLI) core install $(CORE)
	$(CLI) lib install $(LIBS)

clean:
	rm -rf build
