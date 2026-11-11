#!/usr/bin/env python3
"""Pack the practice texts for the tablet: collect/texts/<lang>/<topic>.txt -> texts.tar (<lang>/<topic>.txt and
<lang>/meta.json with line counts). Installed to /home/root/.local/share/rmpp-hwr/texts/ by rmpp-install.sh and
`hwr install`; the tablet's "New practice notebook" reads it.

  python tools/texts.py [OUT.tar]        (default build/texts.tar)

Text files: UTF-8, one sentence per line, '#' comment lines, blank lines between passages.
meta.json (hand-written, per language): {"language": "ko", "topics": {"<topic>": {"name_en", "name_ko",
"generator", "reviewed"}}}; "lines" is filled in here.
"""
import io
import json
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXTS = ROOT / "collect" / "texts"


def build(out):
    out.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(out, "w", format=tarfile.USTAR_FORMAT) as t:
        def add(name, data):
            ti = tarfile.TarInfo(name)
            ti.size, ti.mode, ti.mtime = len(data), 0o644, 0
            t.addfile(ti, io.BytesIO(data))
        for d in sorted(p for p in TEXTS.iterdir() if p.is_dir()):
            meta_f = d / "meta.json"
            meta = json.loads(meta_f.read_text(encoding="utf-8")) if meta_f.exists() else {"language": d.name, "topics": {}}
            if meta.get("language") != d.name:
                sys.exit(f"{meta_f}: language is not {d.name}")
            for f in sorted(d.glob("*.txt")):
                data = f.read_bytes()
                lines = sum(1 for l in data.decode("utf-8").splitlines() if l.strip() and not l.lstrip().startswith("#"))
                meta["topics"].setdefault(f.stem, {"name_en": f.stem, "name_ko": f.stem})["lines"] = lines
                add(f"{d.name}/{f.name}", data)
            missing = [k for k in meta["topics"] if not (d / f"{k}.txt").exists()]
            if missing:
                sys.exit(f"{meta_f}: no text file for topic(s) {', '.join(missing)}")
            add(f"{d.name}/meta.json", (json.dumps(meta, ensure_ascii=False, indent=1) + "\n").encode("utf-8"))
            print(f"{d.name}: {', '.join(f'{k} ({v['lines']})' for k, v in meta['topics'].items())}")
    print(f"wrote {out}")


if __name__ == "__main__":
    build(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "texts.tar")
