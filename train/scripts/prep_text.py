# Build text corpora (Korean/English wiki sentences + word lists), keeping only chars in the model dict.
import re, random, sys, pyarrow.parquet as pq
D = sys.argv[1]
dic = set(l.rstrip('\n') for l in open(f'{D}/dict.txt', encoding='utf-8')) | {' '}
random.seed(1)
def clean(s):
    s = re.sub(r'\s+', ' ', s).strip()
    return s if s and all(c in dic for c in s) else None
def sents(text):
    for para in text.split('\n'):
        for s in re.split(r'(?<=[.?!])\s+', para):
            s = clean(s)
            if s and 4 <= len(s) <= 200: yield s
for name, cap in (('ko', 500000), ('en', 150000)):
    pf = pq.ParquetFile(f'{D}/{name}.parquet'); out = []
    for rg in range(pf.num_row_groups):
        for t in pf.read_row_group(rg, columns=['text']).column('text').to_pylist():
            out.extend(sents(t))
        if len(out) > cap * 3: break
    random.shuffle(out); out = out[:cap]
    open(f'{D}/corpus_{name}.txt', 'w', encoding='utf-8').write('\n'.join(out))
    words = {}
    for s in out:
        for w in s.split(' '):
            w2 = re.sub(r'[^\w]', '', w)
            if name == 'ko' and re.fullmatch(r'[가-힣]{1,8}', w2): words[w2] = words.get(w2, 0) + 1
            if name == 'en' and re.fullmatch(r'[A-Za-z]{2,12}', w2): words[w2] = words.get(w2, 0) + 1
    ws = [w for w, c in sorted(words.items(), key=lambda x: -x[1])][:30000]
    open(f'{D}/words_{name}.txt', 'w', encoding='utf-8').write('\n'.join(ws))
    print(name, len(out), len(ws))
