#!/usr/bin/env python3
"""logger スケッチで記録した CSV を要約する（標準ライブラリのみ）。

使い方:
    python3 tools/log_summary.py data/recordings/still-*.csv

記録がちゃんと取れているか（サンプリング周波数・欠落・指の装着）の確認と、
感情判定に使う特徴量（心拍・動きの大きさ・動きのリズム）のざっくりした値を出す。
"""
import math
import statistics
import sys
from pathlib import Path

ACC_LSB_PER_G = 4096.0      # ±8g
GYRO_LSB_PER_DPS = 65.5     # ±500°/s
FINGER_IR_MIN = 50000       # main.ino と同じ
SATURATION = 32700
RHYTHM_WINDOW_S = 5.0       # リズム判定の窓
RHYTHM_MIN_HZ, RHYTHM_MAX_HZ = 0.5, 3.0
RHYTHM_PEAK_MIN = 0.5       # 自己相関がこれ以上なら「リズムあり」とみなす


def load(path):
    rows, comments, bad = [], [], 0
    header_seen = False
    for line in Path(path).read_text(errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            comments.append(line)
            continue
        if line.startswith("ms,"):
            header_seen = True
            continue
        parts = line.split(",")
        if len(parts) != 9:
            bad += 1  # リセット直後のゴミや、記録終了時に途中で切れた行
            continue
        try:
            rows.append([int(p) for p in parts])
        except ValueError:
            bad += 1
    return rows, comments, bad, header_seen


def percentile(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    k = (len(s) - 1) * p / 100
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def rhythm(motion, rate):
    """窓ごとに動きの大きさの自己相関を取り、0.5〜3Hz で一番強い周期を返す。"""
    n = int(RHYTHM_WINDOW_S * rate)
    lag_min = max(1, int(rate / RHYTHM_MAX_HZ))
    lag_max = int(rate / RHYTHM_MIN_HZ)
    if n <= lag_max or len(motion) < n:
        return []
    results = []
    for start in range(0, len(motion) - n + 1, n):
        w = motion[start:start + n]
        mean = sum(w) / n
        w = [x - mean for x in w]
        var = sum(x * x for x in w)
        if var < 1e-6 * n:  # ほぼ静止
            results.append((0.0, 0.0))
            continue
        best, best_lag = -1.0, lag_min
        for lag in range(lag_min, min(lag_max, n - 1) + 1):
            r = sum(w[i] * w[i + lag] for i in range(n - lag)) / var
            if r > best:
                best, best_lag = r, lag
        results.append((best, rate / best_lag))
    return results


def summarize(path):
    rows, comments, bad, header_seen = load(path)
    print(f"== {path}")
    for c in comments:
        if "error" in c:
            print(f"  !! {c}")
    if not header_seen:
        print("  !! ヘッダ行がありません（logger スケッチが書き込まれているか確認）")
    if len(rows) < 2:
        print("  データがありません")
        return

    ms = [r[0] for r in rows]
    duration = (ms[-1] - ms[0]) / 1000
    rate = (len(rows) - 1) / duration if duration > 0 else float("nan")
    gaps = [b - a for a, b in zip(ms, ms[1:])]
    big_gaps = sum(1 for g in gaps if g > 50)
    print(f"  記録: {duration:.1f}秒 {len(rows)}行  約{rate:.0f}Hz  最大間隔{max(gaps)}ms"
          f"  50ms超の欠落{big_gaps}回  不正行{bad}")

    # --- 心拍 ---
    finger = [r[1] >= FINGER_IR_MIN for r in rows]
    finger_pct = 100 * sum(finger) / len(rows)
    beat_ms = [r[0] for r, f in zip(rows, finger) if r[8] and f]
    ibis = [b - a for a, b in zip(beat_ms, beat_ms[1:])]
    ibis = [i for i in ibis if 270 <= i <= 2000]  # 30〜220 BPM の範囲だけ
    if len(ibis) >= 3:
        bpm = [60000 / i for i in ibis]
        diffs = [b - a for a, b in zip(ibis, ibis[1:])]
        rmssd = math.sqrt(sum(d * d for d in diffs) / len(diffs))
        print(f"  心拍: 指あり{finger_pct:.0f}%  拍{len(beat_ms)}回  BPM中央値{statistics.median(bpm):.0f}"
              f"（10〜90%: {percentile(bpm, 10):.0f}〜{percentile(bpm, 90):.0f}）  RMSSD{rmssd:.0f}ms")
    else:
        print(f"  心拍: 指あり{finger_pct:.0f}%  拍{len(beat_ms)}回（BPM を出すには少なすぎる）")

    # --- 動き（main と同じ「加速度の大きさ − 1g」） ---
    mags = [math.sqrt(r[2] ** 2 + r[3] ** 2 + r[4] ** 2) / ACC_LSB_PER_G for r in rows]
    motion = [abs(m - 1.0) for m in mags]
    motion_ms2 = [m * 9.80665 for m in motion]
    gyro = [math.sqrt(r[5] ** 2 + r[6] ** 2 + r[7] ** 2) / GYRO_LSB_PER_DPS for r in rows]
    sat = sum(1 for r in rows if any(abs(v) >= SATURATION for v in r[2:5]))
    print(f"  動き: 平均{statistics.mean(motion_ms2):.2f}m/s²  中央値{percentile(motion_ms2, 50):.2f}"
          f"  90%点{percentile(motion_ms2, 90):.2f}  最大{max(motion_ms2):.1f}"
          f"  静止時の1g誤差{statistics.median(mags) - 1:+.3f}g  振り切れ{sat}行")
    print(f"  回転: 平均{statistics.mean(gyro):.0f}°/s  90%点{percentile(gyro, 90):.0f}°/s")

    # --- リズム ---
    rh = rhythm(mags, rate)
    if rh:
        peaks = [p for p, _ in rh]
        rhythmic = [(p, f) for p, f in rh if p >= RHYTHM_PEAK_MIN]
        line = (f"  リズム: {RHYTHM_WINDOW_S:.0f}秒窓×{len(rh)}  自己相関の中央値{statistics.median(peaks):.2f}"
                f"  リズムありの窓{100 * len(rhythmic) / len(rh):.0f}%")
        if rhythmic:
            line += f"（周期 中央値{statistics.median(f for _, f in rhythmic):.1f}Hz）"
        print(line)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for p in sys.argv[1:]:
        summarize(p)


if __name__ == "__main__":
    main()
