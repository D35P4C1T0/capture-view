# capture-view

![Capture View logo](docs/assets/logo-readme.png)

Native low-latency HDMI USB capture-card viewer for Linux.

Current features:

- V4L2 device discovery
- manual video format selection
- streaming V4L2 MMAP capture with 2-3 buffers
- newest-frame-only dropping
- MJPEG decode through libjpeg-turbo
- YUYV/NV12 conversion
- SDL3 streaming texture preview
- optional PipeWire audio monitor
- optional PipeWire virtual audio source
- optional v4l2loopback video output
- SDL GPU-backed YUYV/NV12 texture upload path
- optional OpenGL shader render backend
- linear GPU texture upscale for 1440p+ fullscreen viewing
- GTK launcher UI for wizard-style device/mode/audio selection with live device refresh
- Kirsch bitmap font for SDL diagnostics overlays

## Why use it

`capture-view` is built for live HDMI capture monitoring, not media playback or recording. It keeps only the newest frame, drops stale data intentionally, and exposes timing metrics that general-purpose players usually hide.

Use it when you want:

- predictable low-latency preview for consoles or HDMI capture cards
- direct V4L2 capture with explicit format/FPS selection
- PipeWire audio monitoring and optional virtual audio source
- optional v4l2loopback video output for Discord/OBS-style workflows
- a small native Linux tool instead of OBS, browser capture, or a full media player

## Dependencies

Arch Linux:

```sh
sudo pacman -S base-devel cmake ninja pipewire sdl3 gtk4 libjpeg-turbo libglvnd v4l-utils
```

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Windows with vcpkg:

```powershell
vcpkg install sdl3:x64-windows
cmake -S . -B build-windows -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build-windows
```

The Windows build is graphical-first: launching `capture-view.exe` opens the SDL
preview UI with the built-in test pattern. Linux-only V4L2 capture, PipeWire
audio, and GTK launcher features return clear unsupported messages until native
Windows backends are added.

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

GTK launcher:

```sh
./build/capture-view --gtk
```

The GTK UI mirrors the setup wizard for graphical users: choose the capture device, ranked video mode, latency profile, optional PipeWire audio input/output, pacing, scaling, and toggles, refresh devices live, then start the SDL viewer.

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

Expose capture video as a v4l2loopback virtual camera:

```sh
sudo modprobe v4l2loopback video_nr=10 card_label="Capture View" exclusive_caps=1
./build/capture-view --video /dev/video2 --video-output /dev/video10 --format auto
```

The virtual camera can write `yuyv`, `nv12`, `rgba`, or passthrough `mjpeg` frames:

```sh
./build/capture-view --video /dev/video2 --video-output /dev/video10 --video-output-format nv12
```

`mjpeg` output requires MJPEG capture input and forwards compressed frames without re-encoding. Match capture size/FPS and output format to what the receiving app expects.

Write diagnostics to a file while also logging to stderr:

```sh
./build/capture-view --video /dev/video0 --format auto --latency-mode ultra --log-file capture-view.log --verbose
```

Choose frame pacing explicitly:

```sh
./build/capture-view --video /dev/video0 --frame-pacing immediate
./build/capture-view --video /dev/video0 --frame-pacing yield
./build/capture-view --video /dev/video0 --frame-pacing sleep --vsync
./build/capture-view --video /dev/video0 --frame-pacing adaptive
```

Use the explicit OpenGL shader backend if SDL-native texture conversion or presentation is poor on a target GPU. On AMD/Mesa this path uploads raw YUYV/NV12 planes and does color conversion in a fragment shader, avoiding the CPU RGBA conversion path for raw capture modes:

```sh
./build/capture-view --video /dev/video0 --render-backend opengl
```

GPU scaling defaults to bilinear for smoother fullscreen 1080p on 1440p or higher displays. Use bilinear + RCAS sharpening for sharper HUD/text with a small GPU pass:

```sh
./build/capture-view --video /dev/video0 --fullscreen --output-scaling fit \
  --upscale-quality bilinear-rcas --rcas-strength 0.35
```

Best latency options are raw `yuyv`/`nv12` if USB bandwidth allows, `--latency-mode ultra`, `--frame-pacing immediate`, `--no-vsync`, and `--upscale-quality nearest` or `bilinear`. `bilinear-rcas` keeps the same capture path and buffering, but adds one GPU sharpening pass.

Test only audio output with a quiet 440 Hz tone:

```sh
./build/capture-view --test-pattern --audio-test-tone --audio-output "YOUR_SINK_NAME" --log-file audio-test.log
```

Open a capture card:

```sh
./build/capture-view --video /dev/video0 --size 1920x1080 --fps 60 --format auto --no-vsync
```

Open without a titlebar/window controls for cleaner screen sharing or OBS capture:

```sh
./build/capture-view --video /dev/video0 --borderless
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
- `Alt+B`: toggle borderless/titlebar
- `U`: cycle upscale quality
- `[` / `]`: decrease/increase RCAS strength
- `V`: toggle vsync
- `S`: stats overlay
- `G`: GUI overlay
- `?`: keybinding help overlay
- `R`: restart capture
- `A`: restart audio monitor
- `M`: mute audio monitor
- `+` / `-`: audio monitor volume
- `O`: cycle scaling: fit, fill, stretch, integer
- `Esc`: exit fullscreen or quit
- `Q`: quit

Config is saved to `~/.config/lowlat-capture-viewer/config.toml`. Use `--no-config` for temporary runs.

Most V4L2 capture cards can only be opened by one app at a time. Close OBS, Discord, browser tabs, or other viewers if the device is busy.

## Third-party Assets

The SDL overlay font uses Kirsch by molarmanful, bundled under the SIL Open Font License 1.1 in `data/kirsch.OFL.txt`.
