#!/usr/bin/env bash
set -euo pipefail

device="${DEVICE:-/dev/video2}"
size="${SIZE:-1920x1080}"
fps="${FPS:-60}"
format="${FORMAT:-mjpeg}"
duration="${DURATION:-60}"
frame_pacing="${FRAME_PACING:-sleep}"
out_dir="${OUT_DIR:-bench-results/$(date +%Y%m%d-%H%M%S)}"
capture_view="${CAPTURE_VIEW:-./build/capture-view}"

mkdir -p "$out_dir"

capture_log="$out_dir/capture-view.log"
summary="$out_dir/summary.txt"

print_preflight() {
  echo "[device]"
  if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl --list-devices || true
    echo
    v4l2-ctl -d "$device" --all 2>/dev/null | grep -E "Driver name|Card type|Bus info|Device Caps|Video Capture|Metadata|Video Output" || true
    echo
    v4l2-ctl -d "$device" --list-formats-ext || true
  else
    echo "v4l2-ctl not found; install v4l-utils for device diagnostics"
  fi
  echo
  echo "[owners]"
  if command -v fuser >/dev/null 2>&1; then
    fuser -v "$device" 2>&1 || true
  fi
  if command -v lsof >/dev/null 2>&1; then
    lsof "$device" 2>&1 || true
  fi
}

run_capture_view() {
  "$capture_view" \
    --video "$device" \
    --size "$size" \
    --fps "$fps" \
    --format "$format" \
    --benchmark "$duration" \
    --frame-pacing "$frame_pacing" \
    --no-vsync \
    --no-config 2>&1 | tee "$capture_log"
}

{
  echo "v4l2 latency benchmark"
  echo "date=$(date --iso-8601=seconds)"
  echo "device=$device"
  echo "size=$size"
  echo "fps=$fps"
  echo "format=$format"
  echo "duration=$duration"
  echo "frame_pacing=$frame_pacing"
  echo
  echo "[versions]"
  "$capture_view" --version || true
  echo
  print_preflight
  echo
} | tee "$summary"

echo "== capture-view =="
capture_status=0
run_capture_view || capture_status=$?

echo
{
  echo "[capture-view latency result]"
  echo "exit=$capture_status"
  grep 'benchmark_latency rendered=' "$capture_log" | tail -n 1 || echo "no benchmark summary found"
  echo
  echo "logs=$out_dir"
} | tee -a "$summary"

echo
echo "Summary: $summary"
