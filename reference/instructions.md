You are building a native low-latency HDMI USB capture-card viewer for Arch Linux.

Goal:
Create a minimal desktop app that shows video from a UVC/V4L2 HDMI capture card and monitors its audio with the least practical latency. This is for local gameplay/console preview, not recording/editing.

Platform:
- Linux, especially Arch Linux.
- Wayland and X11 compatible.
- PipeWire is the expected audio server.
- The capture card exposes video as /dev/videoX through V4L2 and audio as a separate USB audio/PipeWire/ALSA input.

Preferred implementation language:
- Use C++23 for the first version because direct V4L2, PipeWire, SDL/OpenGL, and libjpeg-turbo integration is straightforward.
- Do not use Electron, Python, Qt Multimedia, VLC/libVLC, OpenCV highgui, or browser/webview rendering.
- Avoid GStreamer for the first version unless implementing an optional backend later. The goal is to control buffering manually.

Core architecture:
1. Device discovery
   - Enumerate /dev/video* devices.
   - Query each device with V4L2:
     - VIDIOC_QUERYCAP
     - VIDIOC_ENUM_FMT
     - VIDIOC_ENUM_FRAMESIZES
     - VIDIOC_ENUM_FRAMEINTERVALS
   - Show supported formats, resolutions, and frame rates.
   - Let the user select:
     - video device
     - resolution
     - FPS
     - pixel format
     - audio input node/device
     - audio output device

2. Format strategy
   - Prefer formats in this order:
     a. YUYV/NV12 at requested FPS if USB bandwidth allows it.
     b. MJPEG if raw formats cannot sustain 1080p60.
   - For cheap USB 2.0 HDMI capture cards, expect 1080p60 or 1080p30 to usually require MJPEG.
   - Implement both:
     - MJPEG decode path using libjpeg-turbo.
     - YUYV path using GPU shader conversion or efficient CPU conversion.
   - Do not add unnecessary scaling unless requested.

3. Video capture
   - Open the V4L2 device in non-blocking mode.
   - Use V4L2 streaming I/O with VIDIOC_REQBUFS and V4L2_MEMORY_MMAP.
   - Use 2 or 3 buffers by default, not 8 or 16.
   - Start stream with VIDIOC_STREAMON.
   - Use poll() or epoll() to wait for ready frames.
   - On readiness:
     - dequeue all currently available buffers with VIDIOC_DQBUF
     - keep only the newest frame for rendering
     - immediately requeue stale/older buffers with VIDIOC_QBUF
   - Frame dropping is intentional. Never build a queue of old frames.
   - The app should optimize for “newest frame now,” not smooth buffered playback.

4. MJPEG decoding
   - Use libjpeg-turbo/TurboJPEG.
   - Decode MJPEG directly to RGBX/BGRX/RGBA-compatible memory for fast texture upload.
   - Reuse decode buffers.
   - Avoid heap allocation per frame.
   - If possible, decode into a persistently allocated staging buffer.
   - Add timing metrics:
     - capture dequeue time
     - JPEG decode time
     - texture upload time
     - render present time

5. Rendering
   - Use SDL3 or GLFW + OpenGL.
   - Preferred: SDL3 window + OpenGL renderer, or SDL3 streaming texture for the MVP.
   - Use a single texture updated every frame.
   - Disable vsync by default for lowest latency.
   - Provide a runtime toggle:
     - vsync off: lowest latency, possible tearing
     - vsync on: smoother, slightly more latency
   - Use triple buffering only if explicitly selected by the user.
   - Render the most recent completed frame only.
   - Do not synchronize video to audio in low-latency mode.

6. Audio
   - Use PipeWire directly through pw_stream.
   - Implement audio monitoring:
     - capture from selected USB HDMI audio input
     - immediately write/play to selected output sink
   - Default format:
     - 48 kHz
     - stereo
     - S16LE or F32LE depending on PipeWire negotiation
   - Keep buffer/quantum small.
   - Target initial audio buffer:
     - 5 ms to 20 ms
   - Make buffer size configurable because too-small buffers may crackle.
   - Do not resample unless required.
   - If resampling is required, log it clearly because it adds latency.
   - Provide separate audio delay adjustment:
     - range: -200 ms to +200 ms
     - default: 0 ms
   - In ultra-low-latency mode, prioritize direct monitoring over perfect A/V sync.

7. Latency behavior
   - The app must never accumulate video frames.
   - If rendering is late, drop frames.
   - If decoding is late, drop frames.
   - Display counters:
     - FPS
     - dropped frames
     - decode time
     - render time
     - audio buffer size
     - estimated audio latency
   - Provide a “panic reset” command that flushes queues and restarts capture.

8. UI
   - Keep the UI minimal:
     - device selector
     - resolution/FPS/format selector
     - audio input selector
     - audio output selector
     - fullscreen toggle
     - mute toggle
     - volume slider
     - vsync toggle
     - latency mode selector
   - Keyboard shortcuts:
     - F: fullscreen
     - M: mute
     - V: toggle vsync
     - S: stats overlay
     - R: restart capture
     - Esc: exit fullscreen

9. Config
   - Store config in:
     ~/.config/lowlat-capture-viewer/config.toml
   - Remember last selected devices and settings.
   - Also provide CLI arguments:
     --video /dev/video0
     --audio-input "name or id"
     --audio-output "name or id"
     --size 1920x1080
     --fps 60
     --format mjpeg|yuyv|nv12
     --fullscreen
     --no-vsync
     --audio-buffer-ms 10

10. OBS/Discord compatibility
   - Do not make OBS mandatory.
   - Optional future feature:
     - output video to v4l2loopback
     - output audio to a PipeWire virtual source
   - For MVP, the app is only a local viewer.
   - It should coexist with OBS only if the capture card supports multiple consumers; otherwise document that most V4L2 capture cards can only be opened by one app at a time.

11. Error handling
   - If the video device is busy, show a clear error.
   - If the audio device is not found, continue video-only and show a warning.
   - If the selected format fails, automatically fall back:
     1080p60 MJPEG → 1080p30 MJPEG → 720p60 YUYV/MJPEG.
   - Log all negotiated formats.

12. Build system
   - Use CMake.
   - Dependencies:
     - libv4l2 or direct linux/videodev2.h ioctls
     - PipeWire 1.0
     - SDL3 or GLFW
     - OpenGL
     - libjpeg-turbo
     - fmt/spdlog optional
   - Provide an Arch Linux PKGBUILD or at least install instructions:
     sudo pacman -S base-devel cmake ninja pipewire sdl3 libjpeg-turbo

13. Testing
   - Add a fake/test mode if possible:
     - generated moving test pattern
     - generated sine audio
   - Add a benchmark mode:
     - run for 60 seconds
     - print average FPS, dropped frames, average decode time, p95 decode time, average render time.
     - compare results against known equivalent viewers such as mpv with matched capture format, resolution, frame rate, vsync setting, and runtime.
   - Add a troubleshooting command:
     --list-devices
   - Add verbose logs:
     --verbose

Implementation priority:
Phase 1:
- V4L2 MJPEG/YUYV capture.
- SDL/OpenGL preview.
- Manual format selection.
- No audio yet.

Phase 2:
- PipeWire audio capture/playback monitoring.
- Audio device selector.
- Mute/volume.
- Audio buffer size config.

Phase 3:
- Stats overlay.
- Config file.
- Fullscreen.
- Latency tuning options.

Phase 4:
- Optional v4l2loopback/PipeWire virtual output for Discord/OBS.
- SDL GPU-backed YUYV/NV12 texture path; add explicit OpenGL shader backend later if needed.
- GUI overlay for live status/diagnostics; richer interactive selectors later.
- Deep frame pacing modes and jitter reporting.
- Comparative benchmark workflow against mpv and similar software.

Most important design rule:
This is not a player. It is a real-time monitor. Always show/play the newest available data and drop anything stale.


Use libjpeg-turbo for MJPEG because it is SIMD-accelerated and is commonly much faster than plain libjpeg on common CPU architectures. For rendering, SDL’s streaming texture model fits the “update pixel data every frame” design well.
