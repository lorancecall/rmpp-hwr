#!/usr/bin/env python3
"""hwr: one command for the personal handwriting model loop.

  hwr install [--model DIR]  install on the tablet (UI add-on, recognizer, model); again after OS updates
  hwr uninstall [--purge]  remove from the tablet
  hwr pull [--keep]        fetch practice notebooks + corrected conversions -> hwr-data/collected;
                           moved conversions are removed from the tablet unless --keep
  hwr practice [N] [--text story|talk|notes|FILE.txt]... [--list]
                           new notebook rmpp-practice-N (N lines, default 100) from the texts, onto the tablet
  hwr practice --v2 [N] [--lang ko|en]
                           structured pages (N pages, default 8) for your language + English + digits;
                           notebooks go into the tablet folder "hwr-db"
  hwr tidy                 move older practice notebooks from the top level into "hwr-db"
  hwr upload               copy hwr-data/collected to the training work dir (data/personal)
  hwr train [NAME]         upload, then personal synth -> fine-tune -> export + judge (see train/README.md)
  hwr status               training jobs and the last lines of the newest log
  hwr fetch NAME           download export/NAME/rec.onnx -> hwr-train/out/NAME, then eval it
  hwr eval [NAME]          error rate (CER) of a model on your eval lines; no NAME = the model on the tablet
  hwr deploy NAME [--as V] install hwr-train/out/NAME on the tablet as the next rmpp-hwr-korean-v0.N
                           (keeps the old model for rollback; versions in hwr-train/out/releases.json)
  hwr rollback             put the previous model back on the tablet
  hwr models               list downloaded models and their eval results
  hwr sync                 copy train/ (jobs, scripts) and the backend settings to the work dir
  hwr setup                one-time setup of the work dir (PaddleOCR, packages, dict)
  hwr dataset PATH NAME    copy a downloaded dataset folder to <work>/data/NAME (e.g. IAM-OnDB -> iamondb)
  hwr job JOB [ARGS] [--res gpu|gpu-short|cpu|cpu-big]
                           run any train/jobs/JOB (e.g. probe.sh, eval.sh export/x/rec.onnx, gen_base.sh)

Settings come from rmpp-hwr/.env (template: .env.example; environment variables win). Training runs on
HWR_BACKEND=local (this computer's GPU) or HWR_BACKEND=slurm (a cluster over ssh, jobs via sbatch).
"""
import argparse
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

DEV = Path(__file__).resolve().parents[2]                 # rmpp-dev
HWR = DEV / "rmpp-hwr"
DATA = Path(os.environ.get("HWR_DATA", DEV / "hwr-data"))
COLLECTED = DATA / "collected"
OUT = DEV / "hwr-train" / "out"
COLLECT = HWR / "collect" / "collect.py"
EXE = HWR / "target" / "release" / ("rmpp-hwr.exe" if os.name == "nt" else "rmpp-hwr")
BASE_MODEL = HWR / "model"                                # base rec.onnx + dict.txt
ORT = DEV / "sdk" / "onnxruntime-win-x64-1.31.1" / "lib" / "onnxruntime.dll"

TRAIN = HWR / "train"                                     # jobs/, scripts/, patches/ copied to the work dir


def load_env(path):
    """KEY=VALUE lines; '#' starts a comment (at line start, or after whitespace)."""
    vals = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            vals[k.strip()] = re.split(r"\s+#", v, maxsplit=1)[0].strip()
    return vals


ENV = load_env(HWR / ".env")


def cfg(key, default=""):
    return os.environ.get(key) or ENV.get(key) or default


def need(key):
    v = cfg(key)
    if not v:
        sys.exit(f"{key} is not set: add it to {HWR / '.env'} (see .env.example)")
    return v


TABLET = cfg("RMPP_HOST", "rmpp")
TABLET_CMDS = {"pull", "practice", "tidy", "eval", "deploy", "rollback", "models", "install", "uninstall"}


def pick_tablet():
    """Use RMPP_HOST, or RMPP_HOST_ALT (e.g. a Tailscale ssh alias) when the first one does not answer."""
    global TABLET
    alt = cfg("RMPP_HOST_ALT")
    if not alt or alt == TABLET:
        return
    probe = lambda h: subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=6", h, "true"],
                                     capture_output=True).returncode == 0
    if not probe(TABLET) and probe(alt):
        print(f"{TABLET} does not answer; using {alt}", file=sys.stderr)
        TABLET = alt
DEV_DIR = "/home/root/.local/share/rmpp-hwr"
XOCHITL = "/home/root/.local/share/remarkable/xochitl"
EVAL_DOC = cfg("HWR_EVAL_DOC")                              # its first EVAL_N lines are never trained on
EVAL_N = int(cfg("HWR_EVAL_N", "12"))


def run(cmd, **kw):
    print("+", cmd if isinstance(cmd, str) else " ".join(map(str, cmd)), file=sys.stderr)
    return subprocess.run(cmd, check=True, **kw)


def ssh(host, script, capture=False):
    r = subprocess.run(["ssh", "-o", "ConnectTimeout=20", host, script], check=True,
                       capture_output=capture, text=True, encoding="utf-8")
    return r.stdout if capture else None


def py():
    return sys.executable


def restart_xochitl():
    ssh(TABLET, "systemctl reset-failed xochitl; systemctl restart xochitl; sleep 2; systemctl is-active xochitl")


# ---- tablet data ----
def cmd_pull(a):
    sidecars = []
    for j in sorted(DATA.glob("practice-*.json")):
        if not (COLLECTED / "practice" / j.name).exists():
            sidecars += ["--sidecar", str(j)]
    run([py(), "-I", str(COLLECT), "pull", "--host", cfg("RMPP_IP") or TABLET, "--key", str(Path(cfg("RMPP_KEY", "~/.ssh/id_ed25519_rmpp")).expanduser()),
         "--out", str(COLLECTED), *sidecars, *(["--keep"] if a.keep else [])], env={**os.environ, "PYTHONIOENCODING": "utf-8"})
    tsv = COLLECTED / "dataset.tsv"
    n = sum(1 for _ in tsv.open(encoding="utf-8")) if tsv.exists() else 0
    print(f"{n} labelled lines in {tsv}")


def cmd_practice(a):
    a.count = a.count or (8 if a.v2 else 100)
    env = {**os.environ, "PYTHONIOENCODING": "utf-8"}
    texts = [x for t in (a.text or []) for x in ("--text", t)]
    if a.list:
        run([py(), "-I", str(COLLECT), "texts", *texts], env=env)
        return
    # rmpp-practice-<lines>, then -<lines>-2, -3 ... when that size exists already
    stem, k = f"practice-{a.count}", 1
    while (DATA / f"{stem}.json").exists():
        k += 1
        stem = f"practice-{a.count}-{k}"
    pdf = DATA / f"{stem}.pdf"
    seed = int(dt.datetime.now().timestamp()) % 1_000_000
    if a.v2:   # structured pages: count = number of pages
        stem, k = f"practice-v2-{a.count}p", 1
        while (DATA / f"{stem}.json").exists():
            k += 1
            stem = f"practice-v2-{a.count}p-{k}"
        pdf = DATA / f"{stem}.pdf"
        run([py(), "-I", str(COLLECT), "practice2", "--out", str(pdf), "--pages", str(a.count), "--seed", str(seed),
             "--lang", a.lang or cfg("HWR_LANG", "ko"), *(["--kinds", a.kinds] if a.kinds else [])], env=env)
    else:
        avoid = [x for j in sorted(DATA.glob("practice-*.json")) for x in ("--avoid", str(j))]
        run([py(), "-I", str(COLLECT), "practice", "--out", str(pdf), "--count", str(a.count), "--seed", str(seed),
             *texts, *avoid], env=env)
    if a.no_upload:
        print(f"wrote {pdf} (not uploaded)")
        return
    stage = Path(tempfile.mkdtemp(prefix=f"{stem}-"))
    folder = tablet_folder(stage)
    run([py(), "-I", str(COLLECT), "make-doc", "--pdf", str(pdf), "--name", f"rmpp-{stem}", "--out", str(stage),
         "--parent", folder], env=env)
    # relative names: scp would read "C:\..." as a host name
    run(["scp", "-q", "-r", *[p.name for p in stage.iterdir()], f"{TABLET}:{XOCHITL}/"], cwd=stage)
    restart_xochitl()
    shutil.rmtree(stage, ignore_errors=True)
    print(f"rmpp-{stem} ({a.count} {'pages' if a.v2 else 'lines'}) is on the tablet, in the folder {FOLDER}")


FOLDER = "hwr-db"   # tablet folder for practice notebooks (pull finds them by name, wherever they are)


def tablet_folder(stage=None):
    """Id of the top-level tablet folder FOLDER. When it is missing and `stage` is given, its files are written
    there (to be copied with the notebook); otherwise None."""
    ids = ssh(TABLET, f"cd {XOCHITL} && for f in $(grep -l CollectionType *.metadata 2>/dev/null); do "
                      f"grep -q 'visibleName.: .{FOLDER}.' $f && ! grep -q 'deleted.: true' $f && echo ${{f%.metadata}}; "
                      "done; true", capture=True).split()
    if ids or stage is None:
        return ids[0] if ids else None
    fid = str(uuid.uuid4())
    meta = {"deleted": False, "lastModified": str(int(time.time() * 1000)), "metadatamodified": False,
            "modified": False, "parent": "", "pinned": False, "synced": False, "type": "CollectionType",
            "version": 0, "visibleName": FOLDER}
    (stage / f"{fid}.metadata").write_text(json.dumps(meta, indent=4))
    (stage / f"{fid}.content").write_text(json.dumps({"tags": []}, indent=4))
    return fid


def cmd_tidy(a):
    """Move practice notebooks that are still at the top level into the FOLDER folder."""
    fid = tablet_folder()
    stage = None
    if not fid:
        stage = Path(tempfile.mkdtemp(prefix="hwr-folder-"))
        fid = tablet_folder(stage)
        run(["scp", "-q", *[p.name for p in stage.iterdir()], f"{TABLET}:{XOCHITL}/"], cwd=stage)
        shutil.rmtree(stage, ignore_errors=True)
    moved = ssh(TABLET, f"cd {XOCHITL} && for f in $(grep -l 'visibleName.: .rmpp-practice' *.metadata 2>/dev/null); do "
                        f"grep -q 'parent.: \"\"' $f && sed -i 's/\"parent\": \"\"/\"parent\": \"{fid}\"/' $f && echo $f; "
                        "done; true", capture=True).split()
    if moved or stage:
        restart_xochitl()
    print(f"{len(moved)} practice notebook(s) moved into {FOLDER}")


# ---- training backends: local GPU or Slurm cluster (settings in .env) ----
# Slurm resources per kind of job; the local backend runs everything on HWR_LOCAL_GPU.
RES = {"gpu": ("gpu", 10, "48G", "06:00:00"), "gpu-short": ("gpu", 4, "16G", "00:40:00"),
       "cpu": ("cpu", 8, "16G", "01:00:00"), "cpu-big": ("cpu", 32, "64G", "03:00:00")}


class Backend:
    def __init__(self):
        self.kind = cfg("HWR_BACKEND", "local")
        if self.kind == "slurm":
            self.host, self.work, self.setup = need("HWR_SLURM_HOST"), need("HWR_SLURM_WORK"), cfg("HWR_SLURM_SETUP")
        elif self.kind == "local":
            self.host, self.work, self.setup = None, need("HWR_LOCAL_WORK"), cfg("HWR_LOCAL_SETUP")
        else:
            sys.exit(f"HWR_BACKEND must be local or slurm, not {self.kind!r}")

    def sh(self, script, capture=False):
        """Run a bash script in the work dir."""
        script = f"cd '{self.work}' && {script}"
        if self.kind == "slurm":
            return ssh(self.host, script, capture=capture)
        r = subprocess.run(["bash", "-c", script], check=True, capture_output=capture, text=True, encoding="utf-8")
        return r.stdout if capture else None

    def site(self):
        """code/site.sh, sourced by jobs/run.sh at the start of every job."""
        lines = ["# written by `hwr sync` from rmpp-hwr/.env", f"export HWR_WORK='{self.work}'"]
        for k in ("HWR_AMP", "HWR_AMP_DTYPE", "HWR_AMP_LEVEL"):
            lines.append(f"export {k}='{cfg(k, {'HWR_AMP': '0', 'HWR_AMP_DTYPE': 'float16', 'HWR_AMP_LEVEL': 'O1'}[k])}'")
        if self.kind == "local":
            lines.append(f"export CUDA_VISIBLE_DEVICES='{cfg('HWR_LOCAL_GPU', '0')}'")
        if self.setup:
            lines.append(self.setup)
        return "\n".join(lines) + "\n"

    def sync(self):
        """train/{jobs,scripts,patches,requirements.txt} + site.sh -> <work>/code"""
        with tempfile.TemporaryDirectory() as t:
            code = Path(t) / "code"
            shutil.copytree(TRAIN, code, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            (code / "site.sh").write_bytes(self.site().encode("utf-8"))
            for f in code.rglob("*.sh"):   # LF line endings for bash on the other side
                f.write_bytes(f.read_bytes().replace(b"\r\n", b"\n"))
            self.put(Path(t), ["code"], ".", clean="code")
        print(f"synced train/ to {self.kind}:{self.work}/code")

    def put(self, src, names, dst_rel, clean=None):
        """Copy names from local dir src into <work>/dst_rel (clean = subdir to delete first)."""
        if self.kind == "slurm":
            tar = subprocess.Popen(["tar", "-C", str(src), "-cf", "-", *names], stdout=subprocess.PIPE)
            pre = f"rm -rf '{self.work}/{clean}'; " if clean else ""
            subprocess.run(["ssh", self.host, f"{pre}mkdir -p '{self.work}/{dst_rel}' && tar -C '{self.work}/{dst_rel}' -xf -"],
                           stdin=tar.stdout, check=True)
            tar.wait()
        else:
            dst = Path(self.work) / dst_rel
            if clean:
                shutil.rmtree(Path(self.work) / clean, ignore_errors=True)
            dst.mkdir(parents=True, exist_ok=True)
            for n in names:
                s = Path(src) / n
                if s.is_dir():
                    shutil.copytree(s, dst / n, dirs_exist_ok=True)
                else:
                    shutil.copy2(s, dst / n)

    def get(self, rel, local_path):
        if self.kind == "slurm":
            run(["scp", "-q", f"{self.host}:{self.work}/{rel}", str(local_path)])
        else:
            shutil.copy2(Path(self.work) / rel, local_path)

    def submit(self, name, steps, env=None):
        """steps = [(job, [args], res)], run in order; each starts only if the previous one succeeded.
        env: extra variables for every step, e.g. {"EPOCHS": "4"} (read by the job scripts)."""
        pre = " ".join(f"{k}={v}" for k, v in (env or {}).items())
        pre = f"env {pre} " if pre else ""
        if self.kind == "slurm":
            prev = None
            for job, args, res in steps:
                kind, cpus, mem, time = RES[res]
                part = need(f"HWR_SLURM_{kind.upper()}_PARTITION")
                opts = ["-p", part, "-c", str(cpus), f"--mem={mem}", "-t", time]
                if cfg(f"HWR_SLURM_{kind.upper()}_QOS"):
                    opts += ["--qos", cfg(f"HWR_SLURM_{kind.upper()}_QOS")]
                if kind == "gpu":
                    opts += [f"--gres={cfg('HWR_SLURM_GPU_GRES', 'gpu:1')}"]
                if cfg("HWR_SLURM_ACCOUNT"):
                    opts += ["-A", cfg("HWR_SLURM_ACCOUNT")]
                if prev:
                    opts += [f"--dependency=afterok:{prev}"]
                jn = f"hwr-{name}-{Path(job).stem}"
                wrap = pre + " ".join(["bash", "code/jobs/run.sh", job, *args])
                prev = self.sh(f"mkdir -p logs && sbatch --parsable -J {jn} -o logs/{jn}-%j.out {' '.join(opts)} "
                               f"--wrap '{wrap}'", capture=True).strip().split(";")[0]
                print(f"submitted {jn}: job {prev}")
        else:
            cmds = [f"{pre}bash code/jobs/run.sh {job} {' '.join(args)} > logs/hwr-{name}-{Path(job).stem}.log 2>&1"
                    for job, args, _ in steps]
            script = (f"cd '{self.work}' && mkdir -p logs && echo running > logs/hwr-{name}.state && "
                      f"( {' && '.join(cmds)} ) && echo done > logs/hwr-{name}.state || echo failed > logs/hwr-{name}.state")
            flags = {"creationflags": 0x00000008 | 0x00000200} if os.name == "nt" else {"start_new_session": True}
            subprocess.Popen(["bash", "-c", script], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, **flags)
            print(f"started hwr-{name} locally in the background ({len(steps)} step(s)); logs in {self.work}/logs")

    def status(self, lines):
        q = ("squeue -u $USER -h -o '%i %j %T %M/%l' | grep ' hwr-' || echo 'no hwr jobs queued'; "
             if self.kind == "slurm" else
             "for f in logs/hwr-*.state; do [ -e \"$f\" ] && echo \"$(basename $f .state): $(cat $f)\"; done; ")
        return self.sh(q + f"f=$(ls -t logs/*.out logs/*.log 2>/dev/null | head -n 1); [ -n \"$f\" ] && echo \"== $f\" && tail -n {lines} $f; "
                       "ls -t export 2>/dev/null | head -n 5; test -f best.txt && echo best: $(cat best.txt) $(cat best_cer.txt 2>/dev/null); true",
                       capture=True)


def cmd_sync(a):
    Backend().sync()


def cmd_setup(a):
    b = Backend()
    b.sync()
    b.submit("setup", [("setup.sh", [], "gpu-short")])


def cmd_job(a):
    b = Backend()
    b.sync()
    b.submit(a.name or Path(a.job).stem, [(a.job, a.args, a.res)])


def excluded():
    """hwr-data/collected/exclude.txt: sample paths (as in dataset.tsv) never used for training or sharing,
    for lines whose writing does not match the label (e.g. other writing in the band). '#' = comment."""
    f = COLLECTED / "exclude.txt"
    if not f.exists():
        return set()
    return {l.split("#")[0].strip() for l in f.read_text(encoding="utf-8").splitlines() if l.split("#")[0].strip()}


def cmd_upload(a):
    if not (COLLECTED / "dataset.tsv").exists():
        sys.exit("nothing collected yet: run `hwr pull` first")
    # eval.txt: the held-out lines (first HWR_EVAL_N of HWR_EVAL_DOC); gen.py never trains on them
    rows = [l.split("\t")[0] for l in (COLLECTED / "dataset.tsv").read_text(encoding="utf-8").splitlines() if "\t" in l]
    held = [p for p in rows if EVAL_DOC and f"/{EVAL_DOC}/" in p and Path(p).stem.isdigit() and int(Path(p).stem) < EVAL_N]
    if not held:
        sys.exit("no eval lines found: set HWR_EVAL_DOC in .env (the practice notebook kept for evaluation)")
    (COLLECTED / "eval.txt").write_text("\n".join(held) + "\n", encoding="utf-8")
    b = Backend()
    skip = excluded()
    with tempfile.TemporaryDirectory() as t:   # dataset.tsv without the lines listed in exclude.txt
        keep = [l for l in (COLLECTED / "dataset.tsv").read_text(encoding="utf-8").splitlines()
                if "\t" in l and l.split("\t")[0] not in skip]
        (Path(t) / "dataset.tsv").write_text("\n".join(keep) + "\n", encoding="utf-8", newline="\n")
        b.put(Path(t), ["dataset.tsv"], "data/personal")
    if skip:
        print(f"left out {len(skip)} line(s) listed in {COLLECTED / 'exclude.txt'}")
    b.put(COLLECTED, ["eval.txt", "samples"], "data/personal")
    print(b.sh("wc -l < data/personal/dataset.tsv", capture=True).strip(), "lines in data/personal")


def cmd_dataset(a):
    """Copy a downloaded dataset folder (e.g. IAM-OnDB with lineStrokes/ and ascii/) to <work>/data/NAME."""
    src = Path(a.path).resolve()
    if not src.is_dir():
        sys.exit(f"{src} is not a folder")
    b = Backend()
    b.put(src.parent, [src.name], "data")
    if src.name != a.name:
        b.sh(f"rm -rf data/{a.name} && mv data/{src.name} data/{a.name}")
    print(b.sh(f"du -sh data/{a.name}", capture=True).strip())


def cmd_train(a):
    name = a.name or "my-" + dt.datetime.now().strftime("%Y%m%d-%H%M")
    b = Backend()
    cmd_upload(a)
    b.sync()
    env = {k: v for k, v in (("EPOCHS", a.epochs), ("LR", a.lr), ("FROM", a.from_)) if v}
    b.submit(name, [("gen_personal.sh", [name], "cpu"), ("train.sh", [name], "gpu"), ("export.sh", [name], "gpu-short")], env)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "last-train.txt").write_text(name + "\n", encoding="utf-8")
    print(f"started {name}; check with `hwr status`, then `hwr fetch {name}`")


def cmd_status(a):
    print(Backend().status(a.lines))


def cmd_fetch(a):
    dst = OUT / a.name
    dst.mkdir(parents=True, exist_ok=True)
    Backend().get(f"export/{a.name}/rec.onnx", dst / "rec.onnx")
    shutil.copy(BASE_MODEL / "dict.txt", dst / "dict.txt")
    print(f"downloaded {dst / 'rec.onnx'}")
    cmd_eval(argparse.Namespace(name=a.name))


# ---- evaluation ----
def lev(x, y):
    d = list(range(len(y) + 1))
    for i, cx in enumerate(x, 1):
        p, d[0] = d[0], i
        for j, cy in enumerate(y, 1):
            p, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, p + (cx != cy))
    return d[-1]


def eval_dir(model_dir):
    rows = [l.rstrip("\n").split("\t") for l in (COLLECTED / "dataset.tsv").open(encoding="utf-8") if "\t" in l]
    evals = [r for r in rows if f"/{EVAL_DOC}/" in r[0] and int(Path(r[0]).stem) < EVAL_N]
    if not evals:
        sys.exit("eval lines not found in hwr-data/collected (run `hwr pull`)")
    env = {**os.environ, "ORT_DYLIB_PATH": str(ORT), "PYTHONIOENCODING": "utf-8"}
    res = subprocess.run([str(EXE), str(model_dir), *[str(COLLECTED / r[0]) for r in rows]],
                         capture_output=True, text=True, encoding="utf-8", env=env, check=True).stdout
    hyp = {}
    for line in res.splitlines():
        m = re.match(r"(.*\.png): (.*?)  \(conf", line)
        if m: hyp[Path(m.group(1)).resolve()] = m.group(2)

    def cer(rs):
        e = n = 0
        for path, gt in rs:
            h = hyp.get((COLLECTED / path).resolve(), "")
            e += lev(gt, h); n += len(gt)
        return e / max(n, 1)
    return cer(evals), cer(rows), len(rows), [(gt, hyp.get((COLLECTED / p).resolve(), "")) for p, gt in evals[:3]]


def cmd_qc(a):
    """Per-line checks of hwr-data/collected before training; writes qc.tsv and lists lines to look at.
    Labels come from the practice prompt (or the corrected text), so a line written differently from its prompt
    (skipped word, typo, extra text) gets a wrong label: those show up as a high base-model CER or an unusual
    width per character."""
    from PIL import Image
    rows = [l.rstrip("\n").split("\t") for l in (COLLECTED / "dataset.tsv").open(encoding="utf-8") if "\t" in l]
    env = {**os.environ, "ORT_DYLIB_PATH": str(ORT), "PYTHONIOENCODING": "utf-8"}
    res = subprocess.run([str(EXE), str(BASE_MODEL), *[str(COLLECTED / r[0]) for r in rows]],
                         capture_output=True, text=True, encoding="utf-8", env=env, check=True).stdout
    hyp = {}
    for line in res.splitlines():
        m = re.match(r"(.*\.png): (.*?)  \(conf ([0-9.]+)", line)
        if m: hyp[Path(m.group(1)).resolve()] = (m.group(2), float(m.group(3)))
    info = []
    for p, t in rows:
        w, h = Image.open(COLLECTED / p).size
        hy, conf = hyp.get((COLLECTED / p).resolve(), ("", 0.0))
        # expected width in "Hangul syllable units": Latin letters, digits and punctuation are narrower
        n = sum(1.0 if "가" <= c <= "힣" else 0.4 if c == " " else 0.6 for c in t)
        info.append(dict(path=p, text=t, hyp=hy, conf=conf, cer=lev(t, hy) / max(len(t), 1), chars=len(t),
                         w48=w * 48 / h, ppc=(w * 48 / h) / max(n, 1)))
    med = sorted(i["ppc"] for i in info)[len(info) // 2]
    skip = excluded()
    grid_dirs = {f.parent.name for f in COLLECTED.glob("samples/practice_*/strokes.json")
                 if '"rmpp-hwr/practice@2"' in f.read_text(encoding="utf-8")[:4000]}
    for i in info:
        f = []
        if i["chars"] > 40: f.append("too-long(>40 chars: dropped by training)")
        if i["cer"] > a.cer: f.append(f"base-CER {i['cer']:.2f} (check label)")
        if i["ppc"] < med * 0.55: f.append("narrow (missing writing?)")
        if i["ppc"] > med * 1.8 and i["path"].split("/")[1] not in grid_dirs:   # v2 cells space letters out
            f.append("wide (extra writing?)")
        if i["path"] in skip: f.append("excluded (exclude.txt)")
        if i["w48"] > 1920: f.append("wider than 1920 px at height 48")
        i["flags"] = "; ".join(f)
    out = COLLECTED / "qc.tsv"
    with out.open("w", encoding="utf-8") as fh:
        fh.write("path\tchars\tbase_cer\tconf\tpx_per_char\tflags\ttext\tbase_output\n")
        for i in info:
            fh.write(f"{i['path']}\t{i['chars']}\t{i['cer']:.3f}\t{i['conf']:.2f}\t{i['ppc']:.1f}\t{i['flags']}\t{i['text']}\t{i['hyp']}\n")
    bad = [i for i in info if i["flags"]]
    print(f"{len(info)} lines, median {med:.1f} px/char at height 48; {len(bad)} flagged -> {out}")
    for i in sorted(bad, key=lambda i: -i["cer"]):
        print(f"  {i['path']}\n    label: {i['text']}\n    base:  {i['hyp']}\n    {i['flags']}")


# ---- open dataset (rmpp-hwr-db) ----
DB_FORMAT = "rmpp-hwr-db/page@1"
OLD_MIX = ["ko", "en", "digits"]


def writer_info(consent=None):
    """hwr-data/writer.json: a random writer id made once on this PC, plus the consent issue number."""
    import uuid
    f = DATA / "writer.json"
    w = json.loads(f.read_text(encoding="utf-8")) if f.exists() else {"writer": str(uuid.uuid4())}
    if consent:
        w["consent_issue"] = int(consent)
    DATA.mkdir(parents=True, exist_ok=True)
    f.write_text(json.dumps(w, indent=1), encoding="utf-8")
    return w


def anonymize(strokes_json, writer):
    """One local strokes.json (practice v2) -> list of rmpp-hwr-db page records. Only the template, the pen data
    and the stroke assignment are kept: no document ids, names, file paths or times."""
    import hashlib
    src = json.loads(strokes_json.read_text(encoding="utf-8"))
    doc = strokes_json.parent.name                       # local folder name; only used inside the hash
    session = hashlib.sha256(f"{writer}:{doc}".encode()).hexdigest()[:16]
    t = src["template"]
    tmpl = {k: t[k] for k in ("format", "page_px", "dpi", "seed", "page_kinds")}
    tmpl["languages"] = t.get("languages") or OLD_MIX      # notebooks made before language mixes: Korean
    out = []
    # same patterns as rmpp-hwr-db/scripts/validate.py: practice prompts that merely look like contact data
    # (random phone numbers, e-mail-like words) are left out rather than rejected by the dataset CI
    private = [re.compile(p) for p in (r"[\w.+-]+@[\w-]+\.[\w.]+", r"https?://", r"www\.",
                                       r"\b01[016789]-?\d{3,4}-?\d{4}\b", r"\b\d{6}-?[1-4]\d{6}\b")]
    for p in sorted(src["pages"], key=int):
        pi = int(p)
        skip = {int(Path(p).stem) for p in excluded() if p.startswith(f"samples/{doc}/") and Path(p).stem.isdigit()}
        items = [i for i in t["items"] if i["page"] == pi and i["id"] not in skip
                 and not any(x.search(i["text"]) for x in private)]
        ids = {i["id"] for i in items}
        assign = [a for a in src["assign"] if a["id"] in ids]
        # Only strokes that belong to a shared item: no stray writing outside the bands/cells (notes, tests,
        # ink inserted by tools), and nothing from left-out items. Indices are renumbered.
        used = sorted({i for a in assign for i in a["strokes"]})
        new = {old: k for k, old in enumerate(used)}
        assign = [{**a, "strokes": [new[i] for i in a["strokes"]],
                   **({"cells": [[new[i] for i in c] for c in a["cells"]]} if "cells" in a else {})} for a in assign]
        page_strokes = src["pages"][p]
        out.append((session, pi, {
            "format": DB_FORMAT, "writer": writer, "session": session, "page": pi,
            "template": {**tmpl, "page_kind": t["page_kinds"][pi]},
            "items": items, "assign": assign,
            "strokes": [{"tool": page_strokes[i]["tool"],
                         "points": [[round(v, 1) for v in pt] for pt in page_strokes[i]["points"]]} for i in used],
        }))
    return out


def cmd_contribute(a):
    """Share practice v2 pages with the open dataset: anonymize, commit to a branch of a fork, open a PR."""
    repo = cfg("HWR_DB_REPO", "lorancecall/rmpp-hwr-db")
    w = writer_info(a.consent)
    if not w.get("consent_issue"):
        sys.exit(f"First join (consent form): https://github.com/{repo}/issues/new?template=join.yml\n"
                 f"then run: hwr contribute --consent <issue number>")
    files = sorted(COLLECTED.glob("samples/practice_*/strokes.json"))
    if not files:
        sys.exit("no practice v2 pages collected yet: `hwr practice --v2`, write them, then `hwr pull`")
    import hashlib
    shared = w.get("shared", {})
    if isinstance(shared, list):                          # older writer.json: names only
        shared = {k: "" for k in shared}
    digest = {f.parent.name: hashlib.sha256(f.read_bytes()).hexdigest()[:16] for f in files}
    # new notebooks, and notebooks written further since the last contribution (their pages are replaced)
    todo = files if a.all else [f for f in files if shared.get(f.parent.name) != digest[f.parent.name]]
    if not todo:
        print("everything collected is already shared"); return
    clone = DATA / "rmpp-hwr-db"
    if not clone.exists():
        run(["gh", "repo", "clone", repo, str(clone)])
    git = lambda *x, **k: run(["git", "-C", str(clone), *x], **k)
    git("fetch", "-q", "origin"); git("checkout", "-q", "-B", "main", "origin/main")
    wid = w["writer"]
    branch = f"contrib/{wid[:8]}-{dt.datetime.now().strftime('%Y%m%d%H%M')}"
    git("checkout", "-q", "-b", branch)
    meta = {"writer": wid, "consent_issue": w["consent_issue"], "license": "CC-BY-4.0"}
    for k in ("handedness", "age_band"):
        if w.get(k): meta[k] = w[k]
    n = 0
    for f in todo:
        q = json.loads((f.parent / "qc.json").read_text(encoding="utf-8")) if (f.parent / "qc.json").exists() else {}
        recs = anonymize(f, wid)
        if not recs:
            continue
        # one folder per language: data/<the writer's language (first of the mix)>/<writer>/
        wdir = clone / "data" / recs[0][2]["template"]["languages"][0] / wid
        wdir.mkdir(parents=True, exist_ok=True)
        (wdir / "writer.json").write_text(json.dumps(meta, indent=1) + "\n", encoding="utf-8", newline="\n")
        for session, pi, rec in recs:
            rec["qc"] = [x for x in q.get("items", []) if x["page"] == pi]
            dst = wdir / session / f"page-{pi:02d}.json"
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(json.dumps(rec, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8", newline="\n")
            n += 1
    git("add", "data")
    run(["git", "-C", str(clone), "-c", "user.name=rmpp-hwr contributor",
         "-c", "user.email=contributor@users.noreply.github.com", "commit", "-q", "-m",
         f"Add {n} page(s) from writer {wid[:8]}"])
    if a.dry_run:
        print(f"dry run: {n} page(s) committed on {branch} in {clone}; nothing pushed"); return
    me = subprocess.run(["gh", "api", "user", "--jq", ".login"], capture_output=True, text=True, check=True).stdout.strip()
    if me.lower() != repo.split("/")[0].lower():          # contributors work from a fork
        run(["gh", "repo", "fork", repo, "--clone=false", "--remote=false"], cwd=clone)
    run(["git", "-C", str(clone), "-c", "credential.helper=", "-c", "credential.helper=!gh auth git-credential",
         "push", "-q", f"https://github.com/{me}/{repo.split('/')[1]}.git", f"HEAD:{branch}"])
    run(["gh", "pr", "create", "-R", repo, "--head", f"{me}:{branch}", "--base", "main",
         "--title", f"Handwriting pages from writer {wid[:8]}",
         "--body", f"Consent: #{w['consent_issue']}\n\n{n} practice page(s), format `{DB_FORMAT}`. "
                   "Created with `hwr contribute`; no personal data."])
    w["shared"] = {**shared, **{f.parent.name: digest[f.parent.name] for f in todo}}
    (DATA / "writer.json").write_text(json.dumps(w, indent=1), encoding="utf-8")


def cmd_eval(a):
    if a.name:
        d = OUT / a.name
    else:  # the model on the tablet
        d = Path(tempfile.mkdtemp(prefix="tablet-model-"))
        run(["scp", "-q", f"{TABLET}:{DEV_DIR}/rec.onnx", f"{TABLET}:{DEV_DIR}/dict.txt", str(d)])
    if not (d / "rec.onnx").exists():
        sys.exit(f"no model at {d}")
    if not (d / "dict.txt").exists():
        shutil.copy(BASE_MODEL / "dict.txt", d / "dict.txt")
    e12, eall, n, ex = eval_dir(d)
    label = a.name or "tablet"
    print(f"{label}: eval CER {e12:.3f} on {EVAL_N} held-out lines, {eall:.3f} on all {n} lines (includes trained lines)")
    for gt, h in ex:
        print(f"  {gt}\n  -> {h}")
    if a.name:
        (d / "eval.json").write_text(json.dumps({"eval12": e12, "all": eall, "lines": n,
                                                 "time": dt.datetime.now().isoformat(timespec="minutes")}), encoding="utf-8")


def cmd_models(a):
    names = {v.get("run"): k for k, v in releases().items()}
    for d in sorted(OUT.glob("*/rec.onnx")):
        j = d.parent / "eval.json"
        r = json.loads(j.read_text(encoding="utf-8")) if j.exists() else {}
        print(f"{d.parent.name:16} {names.get(d.parent.name, ''):22} eval {r.get('eval12', float('nan')):.3f}  "
              f"all {r.get('all', float('nan')):.3f}")
    print(ssh(TABLET, f"echo tablet: $(head -n 1 {DEV_DIR}/model.txt 2>/dev/null || echo base model)", capture=True).strip())


# ---- install on the tablet ----
UI_SO = HWR / "build" / "librmpp-hwr-ui.so"
REC_SO = HWR / "target" / "aarch64-unknown-linux-gnu" / "release" / "librmpp_hwr.so"
GLYPHS = HWR / "build" / "glyphs.json"   # strokes for Convert to ink (ink/glyphs.py build -o build/glyphs.json)


def cmd_install(a):
    """Put rmpp-hwr on the tablet: UI add-on in /usr/lib (loaded by rmpp-preload), recognizer, onnxruntime and
    model in /home. A model already on the tablet (e.g. your personal one) is kept unless --model is given.
    Run again after a reMarkable OS update (it replaces /usr/lib)."""
    ort_so = Path(cfg("HWR_ORT_AARCH64", str(DEV / "sdk" / "onnxruntime-linux-aarch64-1.31.1" / "lib" / "libonnxruntime.so.1.31.1")))
    ui_so, rec_so, glyphs = UI_SO, REC_SO, GLYPHS
    if a.release or not (UI_SO.exists() and REC_SO.exists() and ort_so.exists()):
        # no local build: prebuilt files from the GitHub release
        import urllib.request
        tag = a.release or "latest"
        base = f"https://github.com/{cfg('HWR_REPO', 'lorancecall/rmpp-hwr')}/releases/" + \
               ("latest/download" if tag == "latest" else f"download/{tag}")
        cache = DATA / "release" / tag
        cache.mkdir(parents=True, exist_ok=True)
        for n in ("librmpp-hwr-ui.so", "librmpp_hwr.so", "libonnxruntime.so", "glyphs.json"):
            if not (cache / n).exists() or tag == "latest":
                print(f"downloading {n} ({tag})")
                urllib.request.urlretrieve(f"{base}/{n}", cache / n)
        ui_so, rec_so, ort_so = cache / "librmpp-hwr-ui.so", cache / "librmpp_hwr.so", cache / "libonnxruntime.so"
        glyphs = cache / "glyphs.json"
    if not ssh(TABLET, "test -d /usr/lib/rmpp-preload.d && echo ok || true", capture=True).strip():
        sys.exit("rmpp-preload is not installed on the tablet: `vellum add rmpp-preload` "
                 "(https://github.com/lorancecall/rmpp-packages), then run `hwr install` again")
    has_model = ssh(TABLET, f"test -f {DEV_DIR}/rec.onnx && echo yes || true", capture=True).strip() == "yes"
    model = Path(a.model) if a.model else (None if has_model else BASE_MODEL)
    with tempfile.TemporaryDirectory() as t:
        st = Path(t)
        shutil.copy(ui_so, st / "librmpp-hwr-ui.so.new")
        shutil.copy(rec_so, st / "librmpp_hwr.so.new")
        shutil.copy(ort_so, st / "libonnxruntime.so.new")
        if glyphs.exists():
            shutil.copy(glyphs, st / "glyphs.json.new")
        else:
            print(f"note: {glyphs} missing, Convert to ink needs it (ink/glyphs.py build -o build/glyphs.json)")
        if model:
            shutil.copy(model / "rec.onnx", st / "rec.onnx.new")
            shutil.copy((model / "dict.txt") if (model / "dict.txt").exists() else BASE_MODEL / "dict.txt", st / "dict.txt.new")
            if (model / "model.txt").exists():   # the model's name in Settings > Handwriting
                shutil.copy(model / "model.txt", st / "model.txt.new")
        ssh(TABLET, f"mkdir -p {DEV_DIR}")
        run(["scp", "-q", *[p.name for p in st.iterdir()], f"{TABLET}:{DEV_DIR}/"], cwd=st)
    names = ["librmpp_hwr.so", "libonnxruntime.so"] + (["glyphs.json"] if glyphs.exists() else []) + \
        (["rec.onnx", "dict.txt"] if model else []) + \
        (["model.txt"] if model and (model / "model.txt").exists() else [])
    mv = " && ".join(f"mv -f {DEV_DIR}/{n}.new {DEV_DIR}/{n}" for n in names)
    ssh(TABLET, "set -e\n"
                f"{mv}\n"
                "mount -o remount,rw /\n"
                "mkdir -p /usr/lib/rmpp-hwr\n"
                f"cp {DEV_DIR}/librmpp-hwr-ui.so.new /usr/lib/rmpp-hwr/librmpp-hwr-ui.so.new\n"
                "mv -f /usr/lib/rmpp-hwr/librmpp-hwr-ui.so.new /usr/lib/rmpp-hwr/librmpp-hwr-ui.so\n"
                f"rm -f {DEV_DIR}/librmpp-hwr-ui.so.new\n"
                "ln -sf /usr/lib/rmpp-hwr/librmpp-hwr-ui.so /usr/lib/rmpp-preload.d/60-rmpp-hwr.so\n"
                "sync\n"
                "systemctl reset-failed xochitl || true\n"
                "systemctl restart xochitl\n"
                "sleep 3\n"
                "mount -o remount,ro / || echo 'warning: / is still read-write'\n"
                "systemctl is-active xochitl")
    print(f"rmpp-hwr installed ({'model ' + str(model) if model else 'kept the model on the tablet'}); "
          "Convert page is in the toolbar, settings in Settings > Handwriting")


def cmd_uninstall(a):
    """Remove the UI add-on and the recognizer; with --purge also the model, records and settings."""
    extra = f"rm -rf {DEV_DIR} /home/root/.config/rmpp-hwr\n" if a.purge else \
        f"rm -f {DEV_DIR}/librmpp_hwr.so* {DEV_DIR}/libonnxruntime.so\n"
    ssh(TABLET, "set -e\n"
                "mount -o remount,rw /\n"
                "rm -f /usr/lib/rmpp-preload.d/60-rmpp-hwr.so\n"
                "rm -rf /usr/lib/rmpp-hwr\n"
                "sync\n"
                "systemctl reset-failed xochitl || true\n"
                "systemctl restart xochitl\n"
                "sleep 3\n"
                "mount -o remount,ro / || echo 'warning: / is still read-write'\n" + extra)
    print("rmpp-hwr removed" + (" with all data" if a.purge else "; model, records and settings kept (--purge removes them)"))


# ---- tablet model ----
MODEL_NAME = "rmpp-hwr-korean"          # base model: rmpp-hwr-korean-base; personal models: -v0.1, -v0.2, ...
MODEL_LANG = "ko"                        # language code in models/<version>/pack.json (ko, en, ja, zh, latin)
RELEASES = OUT / "releases.json"          # {"rmpp-hwr-korean-v0.2": {"run": "fix3", "eval12": ..., "time": ...}}


def releases():
    return json.loads(RELEASES.read_text(encoding="utf-8")) if RELEASES.exists() else {}


def release_name(run_name):
    """The version name of a training run: the one it was deployed under before, else the next free v0.N."""
    rel = releases()
    for k, v in rel.items():
        if v.get("run") == run_name:
            return k
    n = max([int(m.group(1)) for k in rel if (m := re.search(r"-v0\.(\d+)$", k))] or [0]) + 1
    return f"{MODEL_NAME}-v0.{n}"


def cmd_deploy(a):
    d = OUT / a.name
    if not (d / "rec.onnx").exists():
        sys.exit(f"no model at {d}: run `hwr fetch {a.name}`")
    r = json.loads((d / "eval.json").read_text(encoding="utf-8")) if (d / "eval.json").exists() else {}
    ver = a.as_ or release_name(a.name)
    desc = ver + (f" (error {r['eval12'] * 100:.0f}%)" if "eval12" in r else "")
    rel = releases()
    rel[ver] = {"run": a.name, **({"eval12": r["eval12"]} if "eval12" in r else {}),
                "time": dt.datetime.now().strftime("%Y-%m-%d")}
    OUT.mkdir(parents=True, exist_ok=True)
    RELEASES.write_text(json.dumps(rel, indent=1) + "\n", encoding="utf-8")
    backup_tablet()
    with tempfile.TemporaryDirectory() as t:
        (Path(t) / "model.txt").write_text(desc + "\n", encoding="utf-8")
        (Path(t) / "pack.json").write_text(json.dumps({"language": MODEL_LANG}) + "\n", encoding="utf-8")
        run(["scp", "-q", "rec.onnx", f"{TABLET}:{DEV_DIR}/"], cwd=d)
        run(["scp", "-q", "model.txt", "pack.json", f"{TABLET}:{DEV_DIR}/"], cwd=t)
    # keep every deployed version under models/<version>/ so Settings > Handwriting can switch between them;
    # pack.json tells the picker which language group the model belongs to
    ssh(TABLET, f"cd {DEV_DIR} && mkdir -p models/{ver} && cp rec.onnx dict.txt model.txt pack.json models/{ver}/")
    restart_xochitl()   # the recognizer caches the model after first use
    print(f"installed: {desc} (also kept in {DEV_DIR}/models/{ver})")


def backup_tablet():
    ssh(TABLET, f"cd {DEV_DIR} && cp rec.onnx rec.onnx.prev && (cp model.txt model.txt.prev 2>/dev/null || rm -f model.txt.prev)")


def cmd_rollback(a):
    ssh(TABLET, f"cd {DEV_DIR} && test -f rec.onnx.prev && mv rec.onnx.prev rec.onnx && "
                f"(test -f model.txt.prev && mv model.txt.prev model.txt || rm -f model.txt)")
    restart_xochitl()
    print("previous model restored")


def main():
    for f in (sys.stdout, sys.stderr): f.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(prog="hwr", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("pull", help="collect from the tablet; corrected conversions are then removed there")
    p.add_argument("--keep", action="store_true", help="leave them on the tablet")
    p.set_defaults(f=cmd_pull)
    p = sp.add_parser("practice", help="new practice notebook rmpp-practice-<count> on the tablet")
    p.add_argument("count", nargs="?", type=int, help="lines to write (default 100); with --v2: pages (default 8)")
    p.add_argument("--text", action="append", help="story / talk / notes / your own .txt (repeatable; default all)")
    p.add_argument("--list", action="store_true", help="show the available texts")
    p.add_argument("--no-upload", action="store_true", help="only write the PDF")
    p.add_argument("--v2", action="store_true", help="structured pages (cells, guides, sizes); count = pages")
    p.add_argument("--kinds", help="v2 page kinds, comma list of grid,digits,english,free (default: a mix)")
    p.add_argument("--lang", help="v2: your language (ko, en; default HWR_LANG or ko); pages mix it with English and digits")
    p.set_defaults(f=cmd_practice)
    sp.add_parser("tidy", help='move practice notebooks at the top level into the tablet folder "hwr-db"').set_defaults(f=cmd_tidy)
    sp.add_parser("upload").set_defaults(f=cmd_upload)
    p = sp.add_parser("qc", help="check collected lines (labels vs writing) before training")
    p.add_argument("--cer", type=float, default=0.6, help="flag lines whose base-model CER is above this")
    p.set_defaults(f=cmd_qc)
    p = sp.add_parser("train"); p.add_argument("name", nargs="?")
    p.add_argument("--epochs", help="training epochs (default 2)"); p.add_argument("--lr", help="learning rate (default 5e-5)")
    p.add_argument("--from", dest="from_", choices=["best", "base"], help="start from best.txt (default) or the base model")
    p.set_defaults(f=cmd_train)
    p = sp.add_parser("status"); p.add_argument("--lines", type=int, default=3); p.set_defaults(f=cmd_status)
    p = sp.add_parser("fetch"); p.add_argument("name"); p.set_defaults(f=cmd_fetch)
    p = sp.add_parser("eval"); p.add_argument("name", nargs="?"); p.set_defaults(f=cmd_eval)
    p = sp.add_parser("deploy", help="install a trained model on the tablet as the next rmpp-hwr-korean-v0.N")
    p.add_argument("name", help="training run (hwr-train/out/NAME)")
    p.add_argument("--as", dest="as_", help="version name to use instead of the next rmpp-hwr-korean-v0.N")
    p.set_defaults(f=cmd_deploy)
    sp.add_parser("rollback").set_defaults(f=cmd_rollback)
    sp.add_parser("models").set_defaults(f=cmd_models)
    p = sp.add_parser("install", help="install rmpp-hwr on the tablet (needs rmpp-preload)")
    p.add_argument("--model", help="model folder with rec.onnx (+ dict.txt) to install; default: keep the tablet's")
    p.add_argument("--release", help="use prebuilt files from this GitHub release tag (default when there is no local build)")
    p.set_defaults(f=cmd_install)
    p = sp.add_parser("uninstall", help="remove rmpp-hwr from the tablet")
    p.add_argument("--purge", action="store_true", help="also delete model, records and settings")
    p.set_defaults(f=cmd_uninstall)
    sp.add_parser("sync").set_defaults(f=cmd_sync)
    sp.add_parser("setup").set_defaults(f=cmd_setup)
    p = sp.add_parser("contribute", help="share practice v2 pages with the open dataset (anonymized PR)")
    p.add_argument("--consent", type=int, help="number of your join issue in the dataset repo (once)")
    p.add_argument("--all", action="store_true", help="also re-share pages shared before")
    p.add_argument("--dry-run", action="store_true", help="commit locally, do not push or open a PR")
    p.set_defaults(f=cmd_contribute)
    p = sp.add_parser("dataset", help="copy a downloaded dataset folder to <work>/data/NAME")
    p.add_argument("path"); p.add_argument("name"); p.set_defaults(f=cmd_dataset)
    p = sp.add_parser("job", help="run train/jobs/JOB on the backend")
    p.add_argument("job"); p.add_argument("args", nargs="*")
    p.add_argument("--res", choices=sorted(RES), default="gpu-short")
    p.add_argument("--name", help="job name (default: the job file name)")
    p.set_defaults(f=cmd_job)
    a = ap.parse_args()
    if a.cmd in TABLET_CMDS:
        pick_tablet()
    a.f(a)


if __name__ == "__main__":
    main()
