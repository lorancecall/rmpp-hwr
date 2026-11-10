# usage: evalonnx.py rec.onnx dict.txt data_root list.txt [list2 ...]  -> CER (greedy CTC, Rust-style bucket padding)
import sys, math, numpy as np, cv2, onnxruntime as ort
import os
BUCKETS = [int(x) for x in os.environ.get('BUCKETS', '160,320,640,960,1280,1920').split(',')]
def lev(a, b):
    d = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        p = d[0]; d[0] = i
        for j, cb in enumerate(b, 1): p, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, p + (ca != cb))
    return d[-1]
model, dic, root = sys.argv[1:4]
chars = [''] + [l.rstrip('\r\n') for l in open(dic, encoding='utf-8').read().split('\n')[:11945]] + [' ']
so = ort.SessionOptions(); so.intra_op_num_threads = 3; so.log_severity_level = 3
sess = ort.InferenceSession(model, so, providers=['CPUExecutionProvider'])
def rec(img):
    tw = img.shape[1]; b = next((x for x in BUCKETS if x >= tw), BUCKETS[-1])
    x = np.zeros((1, 3, 48, b), np.float32); x[:, :, :, :tw] = ((img.astype(np.float32) / 255 - .5) / .5)[None, None]
    p = sess.run(None, {sess.get_inputs()[0].name: x})[0][0]
    used = min(p.shape[0], math.ceil(p.shape[0] * tw / b)); s = ''; prev = 0
    for t in range(used):
        k = int(p[t].argmax())
        if k != 0 and k != prev: s += chars[k]
        prev = k
    return s.strip()
for lst in sys.argv[4:]:
    e = n = e2 = n2 = 0; shown = 0
    rows = [l.rstrip('\n').split('\t') for l in open(lst, encoding='utf-8') if '\t' in l]
    for p, t in rows:
        h = rec(cv2.imread(f'{root}/{p}', 0))
        e += lev(t, h); n += len(t); g2, h2 = t.replace(' ', ''), h.replace(' ', ''); e2 += lev(g2, h2); n2 += len(g2)
        if shown < int(__import__('os').environ.get('SHOW', 0)): print(f'  GT {t}\n  HY {h}'); shown += 1
    print(f'{lst}: n={len(rows)} CER {e / n:.3f} (no spaces {e2 / n2:.3f})')
