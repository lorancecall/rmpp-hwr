"""Golden test for the practice page layout (collect/pages.py): the same seed, language and texts must give the
same sidecar. The tablet's "New practice notebook" (rmpp-hwr UI, QPdfWriter) draws from the same items, so its
output can be checked against tests/golden/practice-<lang>-seed7.json too.

  python tests/test_practice_golden.py           check (exit 1 on a difference); also runs under pytest
  python tests/test_practice_golden.py --update  rewrite the golden files after an intended layout change
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "collect"))
import pages  # noqa: E402

SEED = 7
CASES = {"ko": 8, "en": 8}          # language -> pages


def sidecar(lang, npages):
    pages.TEXTS = HERE / "golden" / "texts"     # fixed texts: the bundled ones may grow
    pages._EN_POOL.clear()
    plan = pages.layout(pages.plan_kinds(npages, lang), SEED, lang)
    return pages.sidecar(plan, SEED, lang)


def check(update=False):
    bad = []
    for lang, n in CASES.items():
        f = HERE / "golden" / f"practice-{lang}-seed{SEED}.json"
        got = sidecar(lang, n)
        if update or not f.exists():
            f.write_text(json.dumps(got, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
            print(f"wrote {f.name}: {len(got['items'])} items")
        elif json.loads(f.read_text(encoding="utf-8")) != got:
            bad.append(f.name)
    return bad


def test_practice_layout_matches_golden():
    assert check() == []


if __name__ == "__main__":
    bad = check("--update" in sys.argv)
    print("differs from golden: " + ", ".join(bad) if bad else "practice layout matches the golden files")
    sys.exit(1 if bad else 0)
