//! Test tool: rmpp-hwr <model-dir> <line.png>...  prints the text, confidence, time and peak memory.
use std::path::Path;
use std::time::Instant;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() >= 3 && args[1] == "--text" {   // rmpp-hwr --text page.rm...: typed text of pages
        for path in &args[2..] {
            let data = std::fs::read(path).expect("read");
            match rmpp_hwr::rm::read_text(&data) {
                Ok(p) => { for l in p { println!("{l}"); } }
                Err(e) => eprintln!("{path}: {e}"),
            }
        }
        return;
    }
    if args.len() >= 3 && args[1] == "--strokes" {   // rmpp-hwr --strokes page.rm...: stroke count or the parse error
        for path in &args[2..] {
            let data = std::fs::read(path).expect("read");
            match rmpp_hwr::rm::read_strokes(&data) {
                Ok(s) => println!("{path}: {} strokes", s.len()),
                Err(e) => println!("{path}: error: {e}"),
            }
        }
        return;
    }
    if args.len() < 3 {
        eprintln!("usage: rmpp-hwr <model-dir> <line.png>...");
        std::process::exit(2);
    }
    let t = Instant::now();
    let rec = rmpp_hwr::Recognizer::open(Path::new(&args[1])).expect("open model");
    eprintln!("open: {} ms", t.elapsed().as_millis());
    for path in &args[2..] {
        if path.ends_with(".rm") {
            let t = Instant::now();
            match rec.page_text(Path::new(path)) {
                Ok(lines) => {
                    for (text, conf) in &lines { println!("{text}  ({conf:.2})"); }
                    eprintln!("page: {} lines in {} ms", lines.len(), t.elapsed().as_millis());
                }
                Err(e) => println!("{path}: error {e}"),
            }
            continue;
        }
        let (gray, w, h) = load_gray(path);
        let t = Instant::now();
        match rec.recognize(&gray, w, h, w) {
            Ok((text, conf)) => println!("{path}: {text}  (conf {conf:.2}, {} ms)", t.elapsed().as_millis()),
            Err(e) => println!("{path}: error {e}"),
        }
    }
    if let Ok(s) = std::fs::read_to_string("/proc/self/status") {
        if let Some(l) = s.lines().find(|l| l.starts_with("VmHWM")) { eprintln!("peak memory: {}", l[6..].trim()); }
    }
}

fn load_gray(path: &str) -> (Vec<u8>, usize, usize) {
    let dec = png::Decoder::new(std::fs::File::open(path).expect("open png"));
    let mut reader = dec.read_info().expect("png");
    let mut buf = vec![0; reader.output_buffer_size()];
    let info = reader.next_frame(&mut buf).expect("frame");
    let (w, h) = (info.width as usize, info.height as usize);
    let ch = info.color_type.samples();
    let gray = (0..w * h).map(|i| buf[i * ch]).collect();
    (gray, w, h)
}
