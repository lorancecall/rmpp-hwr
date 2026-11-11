//! Handwriting recognition for one line of ink (Korean, English, digits), for the reMarkable Paper Pro.
//!
//! Model: PaddleOCR PP-OCRv5 Korean recognition (CTC). Backends: onnxruntime (default, loaded at run time,
//! all cores) or tract (pure Rust). Input: an 8-bit grayscale image of the line, dark ink on light paper.

use std::ffi::{c_char, CStr};
use std::path::Path;

pub mod page;
pub mod rm;

pub type Error = Box<dyn std::error::Error + Send + Sync>;
pub type Result<T> = std::result::Result<T, Error>;

const HEIGHT: usize = 48;
/// Width buckets: a line is padded up to the next one (tract optimizes once per bucket).
const BUCKETS: [usize; 6] = [160, 320, 640, 960, 1280, 1920];

#[cfg(feature = "ort-backend")]
mod backend {
    use super::{Result, HEIGHT};
    use ort::session::{builder::GraphOptimizationLevel, Session};
    use ort::value::Tensor;
    use std::path::Path;
    use std::sync::Mutex;

    pub struct Backend {
        session: Mutex<Session>,
        _env: ort::environment::Environment,
    }

    impl Backend {
        pub fn open(dir: &Path) -> Result<Self> {
            // libonnxruntime.so next to the model, unless ORT_DYLIB_PATH says otherwise
            let lib = std::env::var("ORT_DYLIB_PATH")
                .unwrap_or_else(|_| dir.join("libonnxruntime.so").to_string_lossy().into_owned());
            let env = ort::init_from(lib).map_err(|e| e.to_string())?.build().map_err(|e| e.to_string())?;
            let threads = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(4);
            let mut builder = Session::builder(&env).map_err(|e| e.to_string())?
                .with_optimization_level(GraphOptimizationLevel::Level3).map_err(|e| e.to_string())?
                .with_intra_threads(threads).map_err(|e| e.to_string())?;
            let session = builder.commit_from_file(dir.join("rec.onnx")).map_err(|e| e.to_string())?;
            Ok(Backend { session: Mutex::new(session), _env: env })
        }

        /// Run one [1, 3, 48, width] image; returns (probabilities, time steps, classes).
        pub fn run(&self, width: usize, data: Vec<f32>) -> Result<(Vec<f32>, usize, usize)> {
            let input = Tensor::from_array(([1usize, 3, HEIGHT, width], data)).map_err(|e| e.to_string())?;
            let mut session = self.session.lock().unwrap();
            let outputs = session.run(ort::inputs![input]).map_err(|e| e.to_string())?;
            let (shape, probs) = outputs[0].try_extract_tensor::<f32>().map_err(|e| e.to_string())?;
            Ok((probs.to_vec(), shape[1] as usize, shape[2] as usize))
        }
    }
}

#[cfg(all(feature = "tract-backend", not(feature = "ort-backend")))]
mod backend {
    use super::{Result, HEIGHT};
    use std::collections::HashMap;
    use std::path::{Path, PathBuf};
    use std::sync::Mutex;
    use tract_onnx::prelude::*;

    type Plan = SimplePlan<TypedFact, Box<dyn TypedOp>, Graph<TypedFact, Box<dyn TypedOp>>>;
    pub struct Backend { model: PathBuf, plans: Mutex<HashMap<usize, Plan>> }

    impl Backend {
        pub fn open(dir: &Path) -> Result<Self> {
            Ok(Backend { model: dir.join("rec.onnx"), plans: Mutex::new(HashMap::new()) })
        }

        pub fn run(&self, width: usize, data: Vec<f32>) -> Result<(Vec<f32>, usize, usize)> {
            let mut plans = self.plans.lock().unwrap();
            if !plans.contains_key(&width) {
                let plan = tract_onnx::onnx().model_for_path(&self.model)?
                    .with_input_fact(0, f32::fact([1, 3, HEIGHT, width]).into())?
                    .into_optimized()?.into_runnable()?;
                plans.insert(width, plan);
            }
            let input: Tensor = tract_ndarray::Array4::from_shape_vec((1, 3, HEIGHT, width), data)?.into();
            let out = plans[&width].run(tvec!(input.into()))?;
            let v = out[0].to_array_view::<f32>()?;
            let (t, c) = (v.shape()[1], v.shape()[2]);
            Ok((v.iter().copied().collect(), t, c))
        }
    }
}

pub struct Recognizer {
    backend: backend::Backend,
    chars: Vec<String>,
}

impl Recognizer {
    /// `dir` holds `rec.onnx` and `dict.txt` (and `libonnxruntime.so` for the default backend).
    pub fn open(dir: &Path) -> Result<Self> {
        let dict = std::fs::read_to_string(dir.join("dict.txt"))?;
        // index 0 is the CTC blank; PaddleOCR appends a space at the end
        let mut chars = vec![String::new()];
        chars.extend(dict.lines().map(|l| l.trim_end_matches('\r').to_string()));
        chars.push(" ".to_string());
        Ok(Recognizer { backend: backend::Backend::open(dir)?, chars })
    }

    /// Recognize one line. `gray` is row-major, `stride` bytes per row. Returns (text, mean confidence).
    pub fn recognize(&self, gray: &[u8], w: usize, h: usize, stride: usize) -> Result<(String, f32)> {
        let (x0, y0, x1, y1) = ink_box(gray, w, h, stride).ok_or("no ink")?;
        let (cw, ch) = (x1 - x0 + 1, y1 - y0 + 1);
        // scale to the model height, keeping the aspect ratio (a little margin around the ink)
        let mfrac: f32 = std::env::var("RMPP_HWR_MARGIN").ok().and_then(|v| v.parse().ok()).unwrap_or(0.125);
        let margin = ((ch as f32 * mfrac) as usize).max(2);
        let (bw, bh) = (cw + 2 * margin, ch + 2 * margin);
        let tw = ((bw as f32) * HEIGHT as f32 / bh as f32).round().max(8.0) as usize;
        let bucket = BUCKETS.iter().copied().find(|&b| b >= tw).unwrap_or(*BUCKETS.last().unwrap());
        let tw = tw.min(bucket);

        // bilinear resize into the left of the bucket; the rest stays 0 (PaddleOCR pads with 0 after normalizing)
        let mut data = vec![0f32; 3 * HEIGHT * bucket];
        for ty in 0..HEIGHT {
            let sy = (ty as f32 + 0.5) * bh as f32 / HEIGHT as f32 - 0.5;
            for tx in 0..tw {
                let sx = (tx as f32 + 0.5) * bw as f32 / tw as f32 - 0.5;
                let v = sample(gray, stride, x0 as f32 - margin as f32 + sx, y0 as f32 - margin as f32 + sy, w, h);
                let n = (v / 255.0 - 0.5) / 0.5;
                for c in 0..3 {
                    data[c * HEIGHT * bucket + ty * bucket + tx] = n;
                }
            }
        }
        let (probs, t_len, classes) = self.backend.run(bucket, data)?;
        // CTC greedy decode over the columns that cover the line
        let used = (((t_len as f32) * tw as f32 / bucket as f32).ceil() as usize).min(t_len);
        let (mut text, mut prev, mut conf, mut n) = (String::new(), 0usize, 0f32, 0u32);
        for t in 0..used {
            let row = &probs[t * classes..(t + 1) * classes];
            let (best, p) = row.iter().copied().enumerate()
                .fold((0usize, f32::MIN), |a, (k, v)| if v > a.1 { (k, v) } else { a });
            if best != 0 && best != prev {
                if let Some(s) = self.chars.get(best) { text.push_str(s); }
                conf += p;
                n += 1;
            }
            prev = best;
        }
        Ok((text.trim().to_string(), if n > 0 { conf / n as f32 } else { 0.0 }))
    }
}

impl Recognizer {
    /// Convert a whole `.rm` page: one recognized line per text line, top to bottom.
    pub fn page_text(&self, rm_path: &Path) -> Result<Vec<(String, f32)>> {
        let data = std::fs::read(rm_path)?;
        let strokes = rm::read_strokes(&data)?;
        let mut out = Vec::new();
        for line in page::split_lines(&strokes) {
            let pen: f32 = std::env::var("RMPP_HWR_PEN").ok().and_then(|v| v.parse().ok()).unwrap_or(4.0);
            let (img, w, h) = page::render(&strokes, &line.strokes, pen);
            match self.recognize(&img, w, h, w) {
                Ok(r) if !r.0.is_empty() => out.push(r),
                _ => {}
            }
        }
        Ok(out)
    }
}

fn ink_box(gray: &[u8], w: usize, h: usize, stride: usize) -> Option<(usize, usize, usize, usize)> {
    let (mut x0, mut y0, mut x1, mut y1) = (usize::MAX, usize::MAX, 0, 0);
    for y in 0..h {
        for x in 0..w {
            if gray[y * stride + x] < 160 {
                x0 = x0.min(x); y0 = y0.min(y); x1 = x1.max(x); y1 = y1.max(y);
            }
        }
    }
    (x0 != usize::MAX).then_some((x0, y0, x1, y1))
}

fn sample(gray: &[u8], stride: usize, x: f32, y: f32, w: usize, h: usize) -> f32 {
    let px = |xi: i64, yi: i64| -> f32 {
        if xi < 0 || yi < 0 || xi >= w as i64 || yi >= h as i64 { 255.0 } else { gray[yi as usize * stride + xi as usize] as f32 }
    };
    let (xf, yf) = (x.floor(), y.floor());
    let (dx, dy) = (x - xf, y - yf);
    let (xi, yi) = (xf as i64, yf as i64);
    px(xi, yi) * (1.0 - dx) * (1.0 - dy) + px(xi + 1, yi) * dx * (1.0 - dy)
        + px(xi, yi + 1) * (1.0 - dx) * dy + px(xi + 1, yi + 1) * dx * dy
}

// ---- C ABI, for the xochitl add-on (loaded with dlopen only when the user converts) ----

/// Open the recognizer; `dir` holds the model files. Returns null on failure.
#[no_mangle]
pub extern "C" fn rmpp_hwr_open(dir: *const c_char) -> *mut Recognizer {
    if dir.is_null() { return std::ptr::null_mut(); }
    let dir = unsafe { CStr::from_ptr(dir) }.to_string_lossy().into_owned();
    match Recognizer::open(Path::new(&dir)) {
        Ok(r) => Box::into_raw(Box::new(r)),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Recognize one grayscale line. Writes UTF-8 text (NUL-terminated) to `out`.
/// Returns the text length in bytes, or -1 on error. `confidence` may be null.
#[no_mangle]
pub extern "C" fn rmpp_hwr_recognize(r: *const Recognizer, gray: *const u8, w: u32, h: u32, stride: u32,
                                     out: *mut c_char, out_len: usize, confidence: *mut f32) -> i32 {
    if r.is_null() || gray.is_null() || out.is_null() || out_len == 0 { return -1; }
    let r = unsafe { &*r };
    let img = unsafe { std::slice::from_raw_parts(gray, stride as usize * h as usize) };
    match r.recognize(img, w as usize, h as usize, stride as usize) {
        Ok((text, conf)) => {
            let bytes = text.as_bytes();
            let mut n = bytes.len().min(out_len - 1);
            while n > 0 && !text.is_char_boundary(n) { n -= 1; }
            unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, n);
                *out.add(n) = 0;
                if !confidence.is_null() { *confidence = conf; }
            }
            n as i32
        }
        Err(_) => -1,
    }
}

/// Convert a whole `.rm` page. Writes the lines, separated by newlines, as UTF-8 (NUL-terminated) to `out`.
/// Returns the number of lines, or -1 on error.
#[no_mangle]
pub extern "C" fn rmpp_hwr_page(r: *const Recognizer, rm_path: *const c_char, out: *mut c_char, out_len: usize) -> i32 {
    if r.is_null() || rm_path.is_null() || out.is_null() || out_len == 0 { return -1; }
    let r = unsafe { &*r };
    let path = unsafe { CStr::from_ptr(rm_path) }.to_string_lossy().into_owned();
    match r.page_text(Path::new(&path)) {
        Ok(lines) => {
            let text = lines.iter().map(|l| l.0.as_str()).collect::<Vec<_>>().join("\n");
            let bytes = text.as_bytes();
            let mut n = bytes.len().min(out_len - 1);
            while n > 0 && !text.is_char_boundary(n) { n -= 1; }
            unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, n);
                *out.add(n) = 0;
            }
            lines.len() as i32
        }
        Err(_) => -1,
    }
}

#[no_mangle]
pub extern "C" fn rmpp_hwr_close(r: *mut Recognizer) {
    if !r.is_null() { unsafe { drop(Box::from_raw(r)) }; }
}

/// Typed text of a page (paragraphs joined by '\n') into `out` (NUL-terminated). Needs no model.
/// Returns the byte length, or -1 on error.
#[no_mangle]
pub extern "C" fn rmpp_hwr_text(rm_path: *const c_char, out: *mut c_char, out_len: usize) -> i32 {
    if rm_path.is_null() || out.is_null() || out_len == 0 { return -1; }
    let path = unsafe { CStr::from_ptr(rm_path) }.to_string_lossy().into_owned();
    let Ok(data) = std::fs::read(&path) else { return -1 };
    let Ok(paras) = rm::read_text(&data) else { return -1 };
    let s = paras.join("\n");
    let mut n = s.len().min(out_len - 1);
    while n > 0 && !s.is_char_boundary(n) { n -= 1; }
    unsafe {
        std::ptr::copy_nonoverlapping(s.as_ptr(), out as *mut u8, n);
        *out.add(n) = 0;
    }
    n as i32
}
