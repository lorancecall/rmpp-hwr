import sys, cv2, numpy as np
root, lst, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
rows = [l.rstrip('\n').split('\t') for l in open(lst, encoding='utf-8') if l.strip()][:n]
ims = [cv2.imread(f'{root}/{p}', 0) for p, _ in rows]
W = max(i.shape[1] for i in ims)
out = np.full((len(ims) * 52, W, 3), 255, np.uint8)
for k, i in enumerate(ims): out[k * 52:k * 52 + 48, :i.shape[1]] = i[:, :, None]
cv2.imwrite(sys.argv[4], out)
for p, t in rows: print(p, t)
