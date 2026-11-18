"""The night horizon picture shared by ALKU and U2A (same quantisation)."""
import os
import gfxconv


def alku_horizon():
    """Returns (rows of 16 colour indices 640x150, palette 8 bit RGB, 6 bit source palette)."""
    w, h, rows, _ = gfxconv.load_lbm('ALKU/HOI.LBM')
    pal = gfxconv.asm_db(os.path.join(gfxconv.ROOT, 'ALKU/HOI.IN0'))[16:16 + 768]
    pal = [tuple(pal[i:i + 3]) for i in range(0, 768, 3)]
    pal8 = [tuple(min(255, v * 255 // 63) for v in c) for c in pal]
    pic = gfxconv.indexed_to_rgb(640, 150, rows[:150], pal8)
    idx, qpal = gfxconv.quantize(pic, 16, dither=True)
    order = sorted(range(16), key=lambda i: sum(qpal[i]))
    remap = {o: n for n, o in enumerate(order)}
    qpal = [qpal[o] for o in order]
    idx = [[remap[v] for v in r] for r in idx]
    return idx, qpal, pal
