# Exactly mirrors rmpp-hwr src/lib.rs recognize(): ink box (<160), margin 12.5% (min 2), bilinear resize to height 48.
import numpy as np, cv2
H = 48
def finalize(img, maxw=960):
    ys, xs = np.where(img < 160)
    if len(xs) == 0: return None
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    cw, ch = x1 - x0 + 1, y1 - y0 + 1
    m = max(int(ch * 0.125), 2)
    bw, bh = cw + 2 * m, ch + 2 * m
    can = np.full((bh, bw), 255, np.uint8)
    can[m:m + ch, m:m + cw] = img[y0:y1 + 1, x0:x1 + 1]
    tw = max(8, int(round(bw * H / bh)))
    if tw > maxw: return None
    return cv2.resize(can, (tw, H), interpolation=cv2.INTER_LINEAR)
