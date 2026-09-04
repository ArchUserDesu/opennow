# OpenNOW Xbox 360 / XDK video-performance handoff

## Scope of this handoff

We are fixing the Xbox 360/XDK GeForce NOW stream path by borrowing the useful low-latency/software-rendering ideas from OpenNOW-Switch and OpenNOW-vita.

The user explicitly split the work into two sessions:

- **This session:** port as much of the Switch/Vita stream architecture as is sensible **except FFmpeg frame threading**.
- **Next session:** own the old-FFmpeg frame-threading experiment.

Do not undo working audio byte-order, full-range YUV, controller, authentication, catalog, networking, or XDK deployment fixes.

## Reference ports inspected

### OpenNOW-Switch

Repository: `OpenCloudGaming/OpenNOW-Switch`

Important files inspected:

- `app/src/stream_settings.cpp`
- `app/src/stream/DecodeQueuePolicy.hpp`
- `app/src/webrtc/media.cpp`
- `app/src/stream/ffmpeg/FFmpegVideoDecoder.cpp`
- `app/src/stream/ffmpeg/AVFrameHolder.cpp`
- `app/src/stream/ffmpeg/VideoFrameTiming.hpp`
- `app/src/stream/deko3d/DKVideoRenderer.cpp`
- `app/src/stream/deko3d/SoftwareYuvUpload.hpp`

Switch default preset is **Balanced**:

- `1280x720`
- `60 FPS`
- `12000 kbps`

There is no separate minimum bitrate in the preset definition.

Important Switch software-path ideas:

- compressed-AU queue bounded by FPS, maximum 8 units at 60 FPS
- queue age ceiling of 67 ms rather than allowing deep latency growth
- 2 MiB maximum decode unit
- reusable compressed input-buffer pool
- reusable/bounded decoded-frame storage
- newest/useful-frame bias instead of building display latency
- GPU YUV conversion rather than CPU YUV -> RGB conversion
- software fallback uses FFmpeg frame threading, but that part was explicitly left for the next GPT

### OpenNOW-vita

Repository: `OpenCloudGaming/OpenNOW-vita`

Important files inspected:

- `src/streaming/video/mod.rs`
- `src/streaming/video/worker.rs`
- `src/thread_affinity.rs`
- `src/power.rs`

Useful Vita ideas:

- dedicated media worker
- explicit thread/core placement
- triple-buffer/newest-wins video handoff
- avoid dropping compressed H.264 when an already-decoded pending frame can be superseded instead

Xbox already had explicit worker placement before this session, so that was preserved rather than duplicated.

---

# Baseline before this session

The prior runtime log was the 960x544/60/8 Mbps experiment; GFN delivered 960x540.

The 4-way slice-thread experiment was correctly active:

- requested threads = 4
- actual threads = 4
- active thread type = `FF_THREAD_SLICE`
- `CODEC_FLAG_LOW_DELAY` enabled
- `CODEC_FLAG2_FAST` enabled
- `skip_loop_filter = AVDISCARD_ALL`

But it did not make 60 FPS sustainable.

Representative measurements from the prior log:

- normal sampled decode roughly `23-27 ms`
- large decode spike `84 ms`
- presentation roughly `14 ms`
- queue drops reached `1997` by decoded frame `840`
- RTT in that run was `329 ms`
- audio starvation/hardware-queue pressure was also visible

Conclusion: slice parallelism is insufficient for this GFN bitstream, and the CPU presentation path was also expensive.

---

# What this session changed

## 1. Switch Balanced stream defaults

Commit:

`f77767982aef25116f40ea4d3634043d406dd59c` — `xdk: match Switch balanced stream defaults`

Changed `include/opennow/models.hpp` from:

- 960x544
- 60 FPS
- 8000 kbps

to:

- **1280x720**
- **60 FPS**
- **12000 kbps**

This intentionally matches Switch Balanced exactly.

## 2. Switch-style queue/pool state

Commit:

`b49e7343bb83da6f0d30caab47ffe6ec3fb4becf` — `xdk: add Switch-style video queue pools`

Changed `include/opennow/webrtc_session.hpp`:

- compressed video queue is now a `std::deque<QueuedVideoUnit>`
- each queued AU records enqueue time and whether it is an IDR
- added reusable compressed-AU buffer pool
- added reusable `VideoFrame` pool
- added diagnostics for allocations/reuse/high-water/stale-queue events
- preserved existing explicit Xbox worker affinity

## 3. Low-latency AU queue behavior and buffer reuse

Commit:

`c1d2d588feff9f010e903f7941b1c868fcca251d` — `xdk: port Switch low-latency video queue behavior`

Changed `source/webrtc/webrtc_session.cpp`:

- maximum compressed AU size = **2 MiB**
- queue size follows Switch policy:
  - `clamp((fps + 7) / 8, 2, 8)`
  - therefore 8 units at 60 FPS
- queue also has a **67 ms age limit**
- stale/count-overloaded compressed queues are discarded rather than allowed to grow into latency
- resync remains IDR-safe
- compressed AU allocations are recycled through an 8-buffer pool
- decoded `VideoFrame` plane vectors are recycled through a small pool so FFmpeg output buffers can retain capacity instead of reallocating every frame
- ready-frame behavior remains newest-wins; an unpresented decoded frame can be superseded
- diagnostics now include queue wait, buffer reuse/allocation, queue high-water, stale events, and present drops
- keyframe requests were rate-limited toward the Switch philosophy instead of deliberately encouraging an IDR storm

Do **not** respond to overload by making this queue huge. The 67 ms age cap is intentional.

## 4. GPU YUV -> RGB presentation path

Primary commit:

`ae63d73f2b633381c2d00822503c2f7d7eedcccf` — `xdk: move software YUV conversion into GPU shader`

Sanity-preservation commit:

`72167de38f145a67c6f0322e7d71229974e72826` — `xdk: preserve XAudio callback and UI glyphs`

Changed `source/platform/xdk_platform.cpp`.

Before:

`FFmpeg YUV -> copied VideoFrame planes -> CPU per-pixel YUV->ARGB math -> ARGB texture copy -> trivial pixel shader`

Now, for YUV420P / full-range YUV420P / NV12:

`FFmpeg YUV -> copied/reused VideoFrame planes -> Y + UV texture staging -> GPU pixel-shader YUV->RGB -> display`

Important details:

- supports limited-range and full-range conversion matrices
- preserves the previous full-range/YUVJ420P correction
- existing CPU `to_argb8888()` path remains a fallback
- the first normal stream frame should log:
  - `presentation path=gpu_yuv_shader ...`
- shader init should log:
  - `stream video shaders initialized rgb=1 yuv_gpu=1`

### Why the staging textures are A8R8G8B8

This is deliberately conservative for XDK compatibility.

Rather than guessing that a particular Xbox D3D luminance/two-channel format is lockable and shader-sampleable on this target, the implementation uses the already-proven linear `D3DFMT_LIN_A8R8G8B8` format for Y and UV staging and moves the expensive color math to the GPU.

This means it is **not zero-copy** and it still spends CPU/memory bandwidth expanding Y/UV bytes into texture texels. It should nevertheless remove the expensive per-output-pixel YUV multiply/clamp conversion and avoid building a full intermediate ARGB image on the normal YUV path.

If presentation time remains high, a later optimization is to validate a native single-channel/two-channel or NV12-compatible XDK texture upload path and remove this staging expansion.

---

# What this session intentionally DID NOT change

## FFmpeg threading

**Do not confuse the Switch comparison with the current Xbox decoder state.**

As of this handoff, `source/media/ffmpeg_h264_decoder.cpp` is intentionally still:

- `thread_count = 4`
- `thread_type = FF_THREAD_SLICE`
- LOW_DELAY on
- FAST on
- loop filter skipped

This session did not touch those settings.

The bundled Xbox FFmpeg is old libavcodec major 54 / FFmpeg 1.2-era code. Its threading validation rejects frame threading when `CODEC_FLAG_LOW_DELAY` is set.

The next GPT session owns changing that safely.

## True FFmpeg AVFrame reference handoff

Switch uses `av_frame_clone()`/reference-counted frame objects much more directly.

Xbox's `VideoDecoder` abstraction currently returns a custom `VideoFrame` containing owned vectors. This session reduced allocation churn with frame-vector reuse but did not redesign the public decoder interface around AVFrame references.

## Switch PTS/media-clock display pacing

Switch has a `Present / HoldPrevious / DropSuperseded` policy based on RTP/media timestamps.

Xbox currently does not carry the same target-media-clock plumbing through the custom `VideoFrame`/presentation path. This was not added yet because decoder throughput is still the first-order problem.

Once decode is close to sustainable, PTS-aware pacing is worth porting to improve smoothness and avoid catch-up bursts.

## Hardware H.264 decoding

Not part of this work.

---

# NEXT GPT: primary task — old-FFmpeg frame-threading experiment

The evidence now strongly supports testing frame threading.

Switch's software decoder uses frame threading. Xbox slice threading is active but did not materially improve decode time.

## Required initial experiment

In `source/media/ffmpeg_h264_decoder.cpp`, test approximately:

```cpp
ctx_->thread_count = 4;
ctx_->thread_type = FF_THREAD_FRAME;
```

For this old FFmpeg, remove/disable:

```cpp
CODEC_FLAG_LOW_DELAY
```

because the bundled FFmpeg threading validation refuses frame threading while LOW_DELAY is active.

Keep:

- `CODEC_FLAG2_FAST`
- `skip_loop_filter = AVDISCARD_ALL`
- all working full-range color handling
- audio byte-order fix
- current networking/thread-affinity work

Do not jump straight to 5/6 FFmpeg workers without measurements.

## Diagnostics required

Keep/extend the current startup diagnostics so the runtime log proves:

- requested thread count
- actual thread count
- requested thread type
- `active_thread_type`
- codec capabilities
- slice capability flag
- frame capability flag
- LOW_DELAY state
- FAST state
- loop-filter state

The critical success proof is that `active_thread_type` actually becomes `FF_THREAD_FRAME`.

## Measurements after frame threading

The stream is now 1280x720/60/12 Mbps, so compare:

1. normal H.264 decode time
2. decode spikes
3. queue wait time
4. compressed queue drops/stale events
5. present time
6. present drops
7. audio starvation / XAudio hardware-queue drops
8. WebRTC RTT
9. any extra frame-thread latency before first/displayed picture

### Success target

60 FPS gives 16.67 ms/frame.

At 720p, frame threading needs to produce a large decode improvement. Ideally normal decode should be well below 16.7 ms because presentation and other CPU work still exist.

Do not judge only by average FPS: cloud-gaming latency matters. Frame threading creates frames-in-flight, so measure whether the throughput gain is worth the extra decode latency.

---

# Expected new runtime markers

A healthy run of the work from this session should show markers similar to:

```text
webrtc: start begin target=1280x720 fps=60
webrtc-worker: ... video_queue_max=8 video_queue_age_ms=67 buffer_pool=8 frame_pool=4
xdk-video: stream video shaders initialized rgb=1 yuv_gpu=1
xdk-video: presentation path=gpu_yuv_shader ...
video-perf: ... decode_ms=... queue_wait_ms=... buffer_reuse=.../...
video-present: ... present_ms=... present_drops=...
```

At session stop, inspect:

- `video_queue_drops`
- `video_present_drops`
- `video_buffer_alloc`
- `video_buffer_reuse`
- `video_queue_high`
- `video_queue_stale`

A high reuse count with a low allocation count confirms the pool is working.

---

# If presentation is still expensive

The next renderer optimization after validating this GPU path is **not** to restore CPU YUV->ARGB conversion.

Instead investigate, in this order:

1. Verify which XDK D3D9 linear single-channel / two-channel / NV12-like texture formats are supported by `CreateTexture`, `LockRect`, and pixel-shader sampling.
2. Upload Y and UV without 32-bit expansion if a proven format exists.
3. Consider persistent/preallocated staging surfaces if lock/unlock overhead is significant.
4. Only after the decoder is sustainable, add Switch-style PTS/media-clock frame pacing.

Keep newest-wins presentation behavior and the bounded/time-limited compressed queue; do not hide CPU overload with deep buffering.

---

# Build/deploy workflow

The repo workflow is `.github/workflows/xdk-build.yml` on the self-hosted XDK runner.

The correct validation sequence remains:

1. build exact `main` HEAD
2. compile + link + `imagexex`
3. upload build artifacts/logs
4. FTP deploy `default.xex`
5. SHA-256 verify deployed XEX
6. only then call the XEX built/deployed
7. run on Xbox and collect a fresh `opennow.log`

Do not claim runtime performance is fixed merely because source or CI changed.

---

# Working fixes that remain important

Do not casually revert:

- Xbox Opus/XAudio PCM byte-order representation fix
- nonblocking XAudio submission
- full-range `YUVJ420P` distinction
- `skip_loop_filter = AVDISCARD_ALL` unless measured otherwise
- controller Start/Back pass-through and `LB + RB + Y` OpenNOW menu chord
- analog-stick Y-axis fixes
- NVIDIA client-token session renewal
- fast public All Games catalog
- dedicated Library persisted query
- mixed-case software keyboard
- XNet/XEX HTTPS/network work
- explicit Xbox worker/thread affinity

## Current code HEAD at handoff-writing time

The latest code commit before this handoff document is:

`72167de38f145a67c6f0322e7d71229974e72826`

The handoff document commit itself will naturally be newer than that code commit.
