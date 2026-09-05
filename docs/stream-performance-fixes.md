# Xbox stream performance changes

Baseline: `Z:\Release\opennow.log`, provided September 4, 2026. The log
already confirms 1280x720, three active FFmpeg frame workers, FAST enabled,
and skipped loop filtering. It reports about 25 decoded FPS overall, long
receive gaps, dropped presentations, missing references, and a suspicious
binding-timeout message every second. These are Xbox measurements of the
previous build, not measurements of this revision.

## Implemented directly

- Correct `gettimeofday`: seconds and microseconds now share one FILETIME
  sample. Previously wall seconds were combined with uptime milliseconds,
  introducing backward jumps. Libpeer elapsed deadlines now use GetTickCount.
- Established ICE receive polling does not sleep with the peer/input lock held.
  The dedicated network worker still yields outside that lock when idle.
- Bounded asynchronous logging: 256 records, one background writer, periodic
  flush. Streaming threads enqueue text without console output or disk I/O.
  Overflow drops diagnostic records and reports the count on orderly close.
- Three sets of native linear L8 Y/U/V textures replace expanded ARGB staging.
  At 720p this reduces texture writes from 4,608,000 to 1,382,400 bytes per
  frame. Texture rotation reduces reuse hazards; normal D3D locking still
  protects GPU ownership. Conversion stays on the GPU, H.264 stays on the CPU.
  Full/limited range and the existing RGB fallback remain supported.
- Compressed queue overload stops feeding dependent pictures after reference
  loss, requests a keyframe, and holds the last good image. Decoder-reported
  damaged frames also trigger recovery. The Switch's 67 ms/eight-AU cap,
  1.5-second PLI limit, 720p60/12 Mbps profile and three workers remain.
- Added exact 8-pixel H.264 chroma interpolation using Xenon VMX128 shifts and
  adds (VMX128 has no AltiVec integer multiply). At first use, 6,144 on-console
  comparisons against the bundled scalar FFmpeg cover all fractional positions,
  source alignments, put/average modes, extremes, and patterned pixels. The
  vector dispatch activates only if these pass. Other H.264 kernels remain
  unchanged; this is not a complete NEON-to-VMX port.

## Verification and next runtime check

`python scripts/check-stream-math.py` passes 131,328 host arithmetic comparisons
and reproduces the old clock reversals. This checks the arithmetic model, not
Xbox instruction execution. XDK Release compile/link/imagexex succeeded; see
`xdk/build-performance-final.log`. No host test is Xbox hardware validation.

The new boot marker is `stream runtime=monotonic-clock/native-yuv/async-log`.
After launching the deployed XEX, check for `Xenon chroma VMX scalar comparison
passed` and `native L8 YUV upload`, then compare video-pipeline ingress/decode/
presented FPS, queue drops, receive gaps, and audio starvation over active
gameplay. Sustained 720p60 and Switch-equivalent performance are not yet
established. Native shader sampling and VMX execution require that fresh run.

Deployment target: `192.168.2.204:/Hdd1/Homebrew/default.xex`.
Prior installed binary backup: `xdk/bin/Release/default.before-performance.xex`.
FTP upload completed and a fresh download matched the local 4,956,160-byte
XEX's SHA-256:
`52B289A3C0FAEADA8F1BEF91081373EB7BE5991A4CEDE4324D409FAE9EE4815B`.
