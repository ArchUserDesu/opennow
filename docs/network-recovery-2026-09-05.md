# Streaming recovery revision, September 5, 2026

## Evidence and changes

Inputs were `Z:\Release\opennow-wifi.log` and `opennow-data.log`.
Wi-Fi windows frequently fall below 30 displayed FPS, with receive gaps and
compressed queue recovery. The mobile session establishes media with roughly
292 ms reported RTT, but many windows deliver only 1–3 complete access units
per second. Missing PPS/reference errors precede extended periods without
usable output. These are measurements of the previous Xbox build.

The session request and NVIDIA SDP explicitly disabled dynamic streaming and
fixed minimum, initial and maximum bitrate to the configured 12 Mbps. This
revision restores the pinned Switch reference's mode 3 / DRC negotiation:
4 Mbps minimum, initial max(4 Mbps, configured maximum / 4), and configured
maximum (at least 4 Mbps). Resolution/FPS upper limits remain 720p60 by default.
Server retransmission retention settings also match that reference. They do
not enlarge our compressed decode queue, which remains capped at 67 ms.

Libpeer previously transmitted PLI/NACK but no receiver reports. It now sends
video RR + SDES every 500 ms with sequence/loss, jitter, and sender-report
timing. The statistics include sequence wrap, late packets, duplicates and
confirmed sequence restarts. RTCP parsing checks compound block boundaries;
PLI/NACK header serialization and incoming feedback fields use explicit bytes.
Protocol basis: https://www.rfc-editor.org/rfc/rfc3550.html

FFmpeg now receives a reused, explicitly zero-padded input buffer as required
by its bit readers, and a properly initialized AVPacket. Decoder errors flush
delayed frame-thread output before recovery. PLI requests are coalesced and
rate-limited by the transport worker, removing competing writes to the timer.
A watchdog requests recovery even when no complete access units reach the app.

Runtime marker: `stream revision=dynamic-rr-padded-recovery`.

## Validation

- `scripts/check-stream-host.cmd`: executes production RTCP serializers,
  receiver-statistics code and SDP generation on Windows. Tests exact wire
  bytes, loss fractions, late repair, duplicates, sequence and clock wrap,
  sequence restart, compound SDES layout and bitrate negotiation bounds.
- `python scripts/check-stream-math.py`: 131,328 existing host arithmetic
  comparisons pass.
- `xdk/build-direct.ps1`: Release compile, link and imagexex succeed. Log:
  `xdk/build-network-recovery.log`.
- `scripts/deploy-xex.py`: saves local and remote backups, verifies a staged
  upload, then verifies the installed file by downloading it again.

These host checks and build results do not validate Xbox runtime behavior.

Deployed to `192.168.2.204:/Hdd1/Homebrew/default.xex`, 4,956,160 bytes.
Downloaded installed SHA-256 matches local:
`63ca0b68fccfa988540b00b802de72aee2a347b4a5ecfb0c09307e6944274045`.
Backup filename, both remotely and under `xdk/bin/Release`:
`default.before-20260905T110642Z.xex`.

## Outstanding requirements

This is server-controlled dynamic-streaming negotiation plus standard receiver
feedback, not a proven end-to-end congestion controller. Server response must
be measured on fresh Wi-Fi and mobile sessions. The reference's 4 Mbps floor
remains; operation below it and a minimum 30 FPS are not established. No client
can guarantee a frame-rate/quality floor on an arbitrarily slow or unavailable
network. There is no new client-side resolution/FPS controller in this revision.

H.264 reconstruction remains CPU FFmpeg with the existing Xenon VMX kernel and
GPU YUV presentation. This revision does not implement GPU motion compensation,
inverse transform or reference-picture reconstruction. Microsoft's research
describes moving the motion-compensation feedback loop onto the GPU:
https://www.microsoft.com/en-us/research/publication/accelerate-video-decoding-with-generic-gpu/
The historical implementation is not available here as a raw H.264 decoder API.
A shader decoder requires deterministic correctness tests, GPU reference
storage/synchronization and Xbox profiling before production use. Existing
decoder filter-skipping settings were not changed or quality-validated here.

Next hardware check: launch the new XEX and capture fresh active-gameplay logs
on both connections. Verify the revision marker, server bitrate/resolution
response, displayed FPS, receive gaps, recovery duration and audio starvation.
Deployment alone does not establish production readiness.
