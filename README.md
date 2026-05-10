# capture-view

Native low-latency HDMI USB capture-card viewer for Linux.

This repository is starting with Phase 1 from `reference/instructions.md`:

- V4L2 device discovery
- manual video format selection
- streaming V4L2 MMAP capture with 2-3 buffers
- newest-frame-only dropping
- MJPEG decode through libjpeg-turbo
- YUYV conversion
- SDL3 streaming texture preview
- optional PipeWire audio monitor

## Dependencies

Arch Linux:

```sh
sudo pacman -S base-devel cmake ninja pipewire sdl3 libjpeg-turbo
```

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Package locally:

```sh
makepkg -si
```

## Usage

List video devices and formats:

```sh
./build/capture-view --list-devices
```

List PipeWire audio nodes:

```sh
./build/capture-view --list-audio
```

Troubleshoot environment:

```sh
./build/capture-view --doctor
```

Interactive setup:

```sh
./build/capture-view --wizard --log-file capture-view.log
```

Profiles/config:

```sh
./build/capture-view --wizard --profile console --save-config
./build/capture-view --profile console
./build/capture-view --list-profiles
./build/capture-view --init-profiles
```

Diagnostic bundle:

```sh
./build/capture-view --diagnostic-bundle capture-view-diagnostics.txt
```

Expose capture audio as a PipeWire source for Discord:

```sh
./build/capture-view --video /dev/video2 --format mjpeg --audio-monitor \
  --audio-virtual-source "Capture View" --log-file capture-view.log
```

Discord should then be able to select `Capture View` as an audio source/microphone while you cast the screen.

Write diagnostics to a file while also logging to stderr:

```sh
./build/capture-view --video /dev/video0 --format auto --latency-mode ultra --log-file capture-view.log --verbose
```

Test only audio output with a quiet 440 Hz tone:

```sh
./build/capture-view --test-pattern --audio-test-tone --audio-output "YOUR_SINK_NAME" --log-file audio-test.log
```

Open a capture card:

```sh
./build/capture-view --video /dev/video0 --size 1920x1080 --fps 60 --format auto --no-vsync
```

Enable direct PipeWire monitoring:

```sh
./build/capture-view --video /dev/video0 --audio-monitor --audio-input "alsa_input.usb..." --audio-output "alsa_output..." --audio-buffer-ms 20 --audio-delay-ms 0
```

Run without hardware:

```sh
./build/capture-view --test-pattern
```

Shortcuts:

- `F`: fullscreen
- `V`: toggle vsync
- `S`: stats overlay
- `R`: restart capture
- `A`: restart audio monitor
- `M`: mute audio monitor
- `+` / `-`: audio monitor volume
- `O`: cycle scaling: fit, fill, stretch, integer
- `Esc`: exit fullscreen or quit
- `Q`: quit

Config is saved to `~/.config/lowlat-capture-viewer/config.toml`. Use `--no-config` for temporary runs.

Most V4L2 capture cards can only be opened by one app at a time. Close OBS, Discord, browser tabs, or other viewers if the device is busy.
