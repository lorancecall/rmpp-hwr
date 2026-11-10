//! Whole-page conversion: split pen strokes into text lines, render each line, recognize it.

use crate::rm::Stroke;

pub struct Line {
    pub strokes: Vec<usize>,
    pub y0: f32,
    pub y1: f32,
}

type Box4 = (f32, f32, f32, f32);

fn bbox(s: &Stroke) -> Box4 {
    s.points.iter().fold((f32::MAX, f32::MAX, f32::MIN, f32::MIN), |b, p| {
        (b.0.min(p.0), b.1.min(p.1), b.2.max(p.0), b.3.max(p.1))
    })
}

/// Group strokes into text lines, top to bottom. Small marks (dots, commas) join the nearest line.
pub fn split_lines(strokes: &[Stroke]) -> Vec<Line> {
    let boxes: Vec<Box4> = strokes.iter().map(bbox).collect();
    let mut heights: Vec<f32> = boxes.iter().map(|b| b.3 - b.1).filter(|h| *h > 2.0).collect();
    if heights.is_empty() { return Vec::new(); }
    heights.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let median = heights[heights.len() / 2].max(10.0);
    let tol = median * 0.3;

    let mut order: Vec<usize> = (0..strokes.len()).collect();
    order.sort_by(|&a, &b| ((boxes[a].1 + boxes[a].3) / 2.0).partial_cmp(&((boxes[b].1 + boxes[b].3) / 2.0)).unwrap());
    let is_small = |i: usize| (boxes[i].3 - boxes[i].1) < median * 0.35 && (boxes[i].2 - boxes[i].0) < median * 0.6;

    let mut lines: Vec<Line> = Vec::new();
    for &i in order.iter().filter(|&&i| !is_small(i)) {
        let (y0, y1) = (boxes[i].1, boxes[i].3);
        let cy = (y0 + y1) / 2.0;
        match lines.iter_mut().find(|l| cy >= l.y0 - tol && cy <= l.y1 + tol) {
            Some(l) => { l.y0 = l.y0.min(y0); l.y1 = l.y1.max(y1); l.strokes.push(i); }
            None => lines.push(Line { strokes: vec![i], y0, y1 }),
        }
    }
    for &i in order.iter().filter(|&&i| is_small(i)) {
        let cy = (boxes[i].1 + boxes[i].3) / 2.0;
        let dist = |l: &Line| if cy < l.y0 { l.y0 - cy } else if cy > l.y1 { cy - l.y1 } else { 0.0 };
        match lines.iter_mut().min_by(|a, b| dist(a).partial_cmp(&dist(b)).unwrap()) {
            Some(l) if dist(l) < median => l.strokes.push(i),
            _ => lines.push(Line { strokes: vec![i], y0: boxes[i].1, y1: boxes[i].3 }),
        }
    }
    lines.sort_by(|a, b| a.y0.partial_cmp(&b.y0).unwrap());
    lines
}

/// Render the strokes of one line as an 8-bit grayscale image (black ink on white). Returns (pixels, w, h).
pub fn render(strokes: &[Stroke], idx: &[usize], pen: f32) -> (Vec<u8>, usize, usize) {
    let pad = 12.0;
    let b = idx.iter().map(|&i| bbox(&strokes[i])).fold((f32::MAX, f32::MAX, f32::MIN, f32::MIN), |a, b| {
        (a.0.min(b.0), a.1.min(b.1), a.2.max(b.2), a.3.max(b.3))
    });
    let (ox, oy) = (b.0 - pad, b.1 - pad);
    let (w, h) = ((b.2 - b.0 + 2.0 * pad) as usize + 1, (b.3 - b.1 + 2.0 * pad) as usize + 1);
    let mut img = vec![255u8; w * h];
    let r = pen / 2.0;
    let mut dot = |x: f32, y: f32| {
        let (cx, cy) = (x - ox, y - oy);
        for yy in (cy - r).floor() as i64..=(cy + r).ceil() as i64 {
            for xx in (cx - r).floor() as i64..=(cx + r).ceil() as i64 {
                if xx < 0 || yy < 0 || xx >= w as i64 || yy >= h as i64 { continue; }
                let d2 = (xx as f32 - cx).powi(2) + (yy as f32 - cy).powi(2);
                if d2 <= r * r { img[yy as usize * w + xx as usize] = 0; }
            }
        }
    };
    for &i in idx {
        let pts = &strokes[i].points;
        if pts.len() == 1 { dot(pts[0].0, pts[0].1); }
        for s in pts.windows(2) {
            let (x0, y0, x1, y1) = (s[0].0, s[0].1, s[1].0, s[1].1);
            let steps = ((x1 - x0).hypot(y1 - y0) / 0.5).ceil().max(1.0) as usize;
            for k in 0..=steps {
                let t = k as f32 / steps as f32;
                dot(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
            }
        }
    }
    (img, w, h)
}
