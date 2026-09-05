# GPU H.264 milestone 2: verified reconstruction kernels

Date: 2026-09-05. Status: standalone kernel implementation complete; decoder
integration and Xbox execution remain outstanding.

## Delivered artifact

`patches/h264-gpu/0001-reconstruction-kernels.patch` is unapplied and adds only
`experiments/h264_gpu/`. It has no application/project wiring. A read-only
`git apply --check` passes against the active checkout. Development continues
in the sibling lab from milestone 1. The active tracked diff and production
XEX hash still match the protected baseline. No remote files were touched.

## Implemented and checked

- All 16 H.264 luma qpel positions, with the six-tap filter and unclipped
  diagonal intermediates, including negative vectors and picture-edge clamps.
- All 64 chroma eighth-pixel positions, explicit uni/biweighting and average
  prediction, signed offsets, denominators 0–7, and clipped residual addition.
- FFmpeg-layout 4x4 and 8x8 inverse transforms, including signed int16
  intermediate behavior. The two-pass implementation uses 4/8 coefficient
  fetches per pixel per pass rather than recomputing the complete 2D transform
  at every output pixel.
- Seven shader entry points compile successfully with the actual Xbox XDK
  `fxc.exe`, targeting `ps_3_0`.

The Windows D3D11 WARP harness executes the HLSL. It passed **2,621,440** exact
prediction/weight/residual pixel comparisons and **196,608** exact transform
comparisons. Luma and inverse transforms are checked against function bodies
from the pinned FFmpeg scalar implementation, not a duplicate of our shader.
Chroma and weighting use an independent integer scalar oracle. Generated
FFmpeg files retain their upstream notices and source hashes.

During validation, the harness exposed a D3D11 vertex/pixel input-signature
mismatch: a missing SV_Position declaration caused position data to be read
as texture coordinates. Fixing the signature resolved the mismatch; the final
run passes without shader warnings. This reinforces why compiler success
alone is insufficient.

## Limits and next milestone

WARP is desktop software shader execution. It validates neither Xenos texture
sampling/endianness nor GPU throughput. Current R32F resources prioritize exact
numeric validation and are not a production bandwidth decision. The test
corpus is synthetic, not recorded GFN pictures. These are reconstruction
kernels, not a complete GPU-assisted H.264 decoder.

Next: bounded job/reference ownership and a standalone Xbox validation runner
using explicit resolve/readback and timing. No production deployment is allowed
at this stage. Intra prediction, deblocking, FFmpeg job extraction and safe DPB
fallback must still be completed before integration.
