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
mpv_bin="${MPV:-mpv}"
mpv_style="${MPV_STYLE:-exact}"
mpv_no_config="${MPV_NO_CONFIG:-0}"
mpv_vo="${MPV_VO:-}"

mkdir -p "$out_dir"

capture_log="$out_dir/capture-view.log"
mpv_log="$out_dir/mpv.log"
summary="$out_dir/summary.txt"

mpv_input_format() {
  case "$format" in
    mjpeg) echo "mjpeg" ;;
    yuyv) echo "yuyv422" ;;
    nv12) echo "nv12" ;;
    *) echo "$format" ;;
  esac
}

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

run_mpv() {
  local input_format
  input_format="$(mpv_input_format)"
  local -a cmd=(
    "$mpv_bin"
    "av://v4l2:$device"
    --profile=low-latency
    --untimed
    --demuxer-lavf-o="input_format=$input_format,video_size=$size,framerate=$fps"
  )
  if [[ "$mpv_style" == "instrumented" ]]; then
    cmd+=(
      --demuxer-lavf-format=video4linux2
      --no-cache
      --vd-lavc-threads=1
      --no-audio
      --video-sync=display-desync
      --term-status-msg='time=${time-pos} fps=${estimated-vf-fps} dropped=${vo-drop-frame-count} mistimed=${mistimed-frame-count}'
    )
  fi
  if [[ "$mpv_no_config" == "1" ]]; then
    cmd+=(--no-config)
  fi
  if [[ -n "$mpv_vo" ]]; then
    cmd+=("--vo=$mpv_vo")
  fi

  printf 'mpv command:'
  printf ' %q' "${cmd[@]}"
  printf '\n'
  if command -v timeout >/dev/null 2>&1; then
    set +e
    timeout --foreground "${duration}s" "${cmd[@]}" 2>&1 | tee "$mpv_log"
    local status=${PIPESTATUS[0]}
    set -e
    if [[ "$status" == "124" ]]; then
      echo "mpv stopped by timeout after ${duration}s" | tee -a "$mpv_log"
      return 0
    fi
    return "$status"
  fi
  echo "timeout not found; close mpv manually after ${duration}s" | tee -a "$mpv_log"
  "${cmd[@]}" 2>&1 | tee -a "$mpv_log"
}

{
  echo "v4l2 benchmark"
  echo "date=$(date --iso-8601=seconds)"
  echo "device=$device"
  echo "size=$size"
  echo "fps=$fps"
  echo "format=$format"
  echo "duration=$duration"
  echo "frame_pacing=$frame_pacing"
  echo "mpv_style=$mpv_style"
  echo
  echo "[versions]"
  "$capture_view" --version || true
  "$mpv_bin" --version | head -n 1 || true
  echo
  print_preflight
  echo
} | tee "$summary"

echo "== capture-view =="
capture_status=0
run_capture_view || capture_status=$?

echo
echo "== mpv =="
mpv_status=0
run_mpv || mpv_status=$?

{
  echo
  echo "[capture-view result]"
  echo "exit=$capture_status"
  grep 'benchmark rendered=' "$capture_log" | tail -n 1 || echo "no benchmark summary found"
  echo
  echo "[mpv result]"
  echo "exit=$mpv_status"
  grep -E 'time=|Video  --vid|Exiting|Error|error|failed|No JPEG data|Can not process|Found EOI|VIDIOC' "$mpv_log" | tail -n 40 || true
  echo
  echo "logs=$out_dir"
} | tee -a "$summary"

echo
echo "Summary: $summary"
