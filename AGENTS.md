# Agent notes for OpenNOW-Xenon

Target: Xbox 360 RGH/JTAG, Free60 LibXenon, big-endian Xenon PowerPC. The application flow is already wired end to end; preserve it while fixing toolchain/runtime issues.

Rules: keep network/GFN packet byte order explicit; keep H.264 CPU-only until hardware profiling justifies acceleration; target 720p60 first; avoid deep video queues; do not re-depacketize H.264 after libpeer's `PeerVideoPacket`; preserve NVIDIA signaling/input behavior from the pinned OpenNOW reference; keep audio 48 kHz stereo for `xenon_sound`; and never describe a host test as Xbox hardware validation.
