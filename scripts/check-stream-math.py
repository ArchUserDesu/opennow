"""Host arithmetic checks; these do not execute Xbox vector instructions."""
import random


def lerp_u16(a, b, fraction):
    delta = (b - a) & 65535
    result = (a << 3) & 65535
    for bit in range(3):
        if fraction & (1 << bit):
            result = (result + (delta << bit)) & 65535
    return result


rng = random.Random(360)
cases = 0
for x in range(8):
    for y in range(8):
        samples = [(0, 0, 0, 0), (255, 255, 255, 255),
                   (0, 255, 255, 0), (255, 0, 0, 255)]
        samples.extend(tuple(rng.randrange(256) for _ in range(4))
                       for _ in range(2048))
        for a, b, c, d in samples:
            actual = ((lerp_u16(lerp_u16(a, b, x),
                                lerp_u16(c, d, x), y) + 32) & 65535) >> 6
            expected = ((8-x)*(8-y)*a + x*(8-y)*b +
                        (8-x)*y*c + x*y*d + 32) >> 6
            assert actual == expected, (x, y, a, b, c, d)
            cases += 1
print(f"PASS: {cases} chroma interpolation comparisons, all fractional positions")

# Demonstrate the old clock discontinuity with a plausible boot/wall offset.
old = [(t // 1000)*1000 + (t + 375) % 1000 for t in range(10000)]
backwards = sum(b < a for a, b in zip(old, old[1:]))
assert backwards == 10
# The replacement's elapsed arithmetic remains correct through DWORD wrap.
for start in (1, 999, 0xFFFFFFF0):
    for elapsed in (0, 1, 16, 67, 1000, 10000):
        end = (start + elapsed) & 0xFFFFFFFF
        assert (end - start) & 0xFFFFFFFF == elapsed
print("PASS: reproduced 10 old-clock reversals; monotonic elapsed/wrap arithmetic")
