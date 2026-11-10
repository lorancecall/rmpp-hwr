# WiSE-FT (Wortsman et al., arXiv 2109.01903): blend base and fine-tuned weights, w = (1-a)*base + a*fine.
# Keeps what the base model knew (English, print) while keeping part of the fine-tuned gain.
# usage: wise_ft.py <base prefix> <fine-tuned prefix> <alpha> <out prefix>     (prefixes without .pdparams)
import sys
import numpy as np
import paddle

base, fine, alpha, out = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
b, f = paddle.load(base + '.pdparams'), paddle.load(fine + '.pdparams')
npy = lambda v: v.numpy() if hasattr(v, 'numpy') else np.asarray(v)
mixed, kept = {}, 0
for k, v in f.items():
    fv = npy(v)
    bv = npy(b[k]) if k in b else None
    if bv is not None and bv.shape == fv.shape and np.issubdtype(fv.dtype, np.floating):
        mixed[k] = ((1 - alpha) * bv.astype(np.float32) + alpha * fv.astype(np.float32)).astype(fv.dtype)
    else:   # integer counters, shape changes: take the fine-tuned value
        mixed[k] = fv; kept += 1
paddle.save(mixed, out + '.pdparams')
print(f'{out}.pdparams: alpha={alpha}, {len(mixed)} tensors, {kept} copied from the fine-tuned model')
