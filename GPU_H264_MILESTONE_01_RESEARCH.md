# GPU H.264 milestone 1: architecture and isolation

Date: 2026-09-05. Status: research/design complete; decoder not implemented.

## Protected baseline

The active checkout remains untouched, including the existing uncommitted
network-recovery revision. Development is isolated in the sibling directory
`../opennow-gpu-lab-20260905`. Its `baseline.json` records HEAD, branch, a hash
of the complete tracked diff, and the production XEX hash. `lab_admin.py verify`
detects changes to these. No FTP operations are part of this experiment.

Only milestone reports and unapplied patch artifacts will be added here until
the complete decoder passes correctness and Xbox performance gates. Patches
will include standalone reproduction instructions and will not be wired into
the application build. Existing unrelated work will be preserved at integration.

## Research findings

Microsoft's generic-GPU research describes a CPU/GPU pipeline with reference
reconstruction kept on the GPU to avoid readback. It is not a downloadable
Xbox raw H.264 decoder API:
https://www.microsoft.com/en-us/research/publication/accelerate-video-decoding-with-generic-gpu/

The Ghent H.264 paper gives a concrete shader design: quads describe motion
partitions; shader passes implement subpixel prediction and residual addition.
Its limitations matter: the early version requires constrained intra prediction,
supports one reference picture, and omits the in-loop deblocking filter. These
restrictions cannot silently be imposed on a GFN bitstream. The later paper
extends reference storage and discusses P/B reconstruction. Neither paper's
desktop performance establishes Xenon/Xenos performance.
https://biblio.ugent.be/publication/416757/file/448055.pdf
https://biblio.ugent.be/publication/750433/file/829300.pdf

The exact Xbox SPIE paper (66960X, Baeza/Chen/Christoffersen/Dinu/Friemel)
has been identified bibliographically but its full text has not been obtained.
Do not attribute details from the Ghent or generic-GPU papers to ATI's code.

Installed XDK headers/compiler provide pixel shaders, point-sampled textures,
floating-point formats, render targets and explicit EDRAM resolves. Shader
compilation can be checked locally; sampling precision, endian interpretation,
resolve behavior and costs require Xbox execution.

## Implementation decisions

1. Retain FFmpeg's entropy parser, SPS/PPS handling, reference-list derivation
   and error recovery. The interception point is before
   `ff_h264_hl_decode_mb`, where reconstruction inputs still exist. The existing
   `PeerVideoPacket` remains a complete AU; do not depacketize again.
2. Begin with deterministic 8-bit 4:2:0 reconstruction kernels: all 16 luma
   quarter-pixel positions, all 64 chroma eighth-pixel positions, explicit
   weighting, residual addition and inverse transforms. Use point sampling
   and explicit integer-equivalent rounding/clipping, not texture bilinear
   interpolation as a substitute for the six-tap H.264 luma filter.
3. Compare kernels against the pinned FFmpeg scalar implementations. Compile
   with the real XDK shader compiler. Desktop shader execution is an additional
   check only; it is never Xbox validation.
4. Keep decoded references on the GPU only after intra dependencies, in-loop
   deblocking, long-term references and GPU completion fences are implemented.
   Never publish a partially reconstructed reference. Do not add per-block
   GPU readback to the production path.
5. Unsupported syntax or lost resources must select CPU recovery explicitly.
   Switching back requires valid reference contents or an IDR; a capability
   flag alone cannot make fallback safe.

## Major milestones and completion gates

- M1: research and protected baseline (this report).
- M2: exact reconstruction kernels, shader compilation, reproducible patch.
- M3: bounded job/reference lifecycle and a standalone Xbox validation build.
- M4: FFmpeg extraction plus intra/deblock/reference reconstruction, full
  bitstream comparisons against CPU decoding, malformed-stream recovery.
- M5: Xbox correctness and 720p profiling, then all-at-once integration,
  final XDK build and verified deployment.

No milestone may describe shader compilation or synthetic host tests as a
complete GPU-assisted decoder. Completion requires measured end-to-end benefit,
unchanged reconstruction quality and bounded latency on Xbox.
