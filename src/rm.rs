//! Minimal reader for reMarkable v6 page files (`.rm`): pen strokes only.
//! Format notes follow rmscene (https://github.com/ricklupton/rmscene, MIT).

pub struct Stroke {
    pub tool: u32,
    pub points: Vec<(f32, f32, f32)>, // x, y, width
}

const HEADER: &[u8] = b"reMarkable .lines file, version=6";
const BLOCK_LINE: u8 = 0x05;
const ITEM_LINE: u8 = 0x03;
// eraser tools: their strokes are not ink
const ERASERS: [u32; 3] = [6, 8, 18];

struct Cur<'a> { b: &'a [u8], p: usize }

impl<'a> Cur<'a> {
    fn left(&self) -> usize { self.b.len().saturating_sub(self.p) }
    fn u8(&mut self) -> Option<u8> { let v = *self.b.get(self.p)?; self.p += 1; Some(v) }
    fn bytes(&mut self, n: usize) -> Option<&'a [u8]> {
        let s = self.b.get(self.p..self.p + n)?; self.p += n; Some(s)
    }
    fn u16(&mut self) -> Option<u16> { Some(u16::from_le_bytes(self.bytes(2)?.try_into().ok()?)) }
    fn u32(&mut self) -> Option<u32> { Some(u32::from_le_bytes(self.bytes(4)?.try_into().ok()?)) }
    fn f32(&mut self) -> Option<f32> { Some(f32::from_le_bytes(self.bytes(4)?.try_into().ok()?)) }
    fn f64(&mut self) -> Option<f64> { Some(f64::from_le_bytes(self.bytes(8)?.try_into().ok()?)) }
    fn varuint(&mut self) -> Option<u64> {
        let (mut v, mut shift) = (0u64, 0);
        loop {
            let b = self.u8()?;
            v |= ((b & 0x7f) as u64) << shift;
            if b & 0x80 == 0 { return Some(v); }
            shift += 7;
            if shift > 63 { return None; }
        }
    }
    /// Read a tag and check its index and type; on mismatch rewind and return None.
    fn tag(&mut self, index: u64, kind: u64) -> Option<()> {
        let save = self.p;
        let t = self.varuint()?;
        if t >> 4 == index && t & 0xf == kind { Some(()) } else { self.p = save; None }
    }
    fn id(&mut self, index: u64) -> Option<(u8, u64)> { self.tag(index, 0xf)?; Some((self.u8()?, self.varuint()?)) }
    fn int(&mut self, index: u64) -> Option<u32> { self.tag(index, 0x4)?; self.u32() }
    fn float(&mut self, index: u64) -> Option<f32> { self.tag(index, 0x4)?; self.f32() }
    fn double(&mut self, index: u64) -> Option<f64> { self.tag(index, 0x8)?; self.f64() }
    fn sub(&mut self, index: u64) -> Option<Cur<'a>> {
        self.tag(index, 0xc)?;
        let n = self.u32()? as usize;
        Some(Cur { b: self.bytes(n)?, p: 0 })
    }
}

/// Pen strokes of one page, in page coordinates (x is centred on 0). Deleted and eraser strokes are skipped.
pub fn read_strokes(data: &[u8]) -> Result<Vec<Stroke>, String> {
    if !data.starts_with(HEADER) { return Err("not a v6 .rm file".into()); }
    let mut c = Cur { b: data, p: 43 };
    let mut out = Vec::new();
    while c.left() >= 8 {
        let len = c.u32().ok_or("truncated")? as usize;
        let _unknown = c.u8();
        let _min_version = c.u8();
        let version = c.u8().ok_or("truncated")?;
        let kind = c.u8().ok_or("truncated")?;
        let body = c.bytes(len).ok_or("truncated block")?;
        if kind != BLOCK_LINE { continue; }
        if let Some(s) = line_block(body, version) { out.push(s); }
    }
    Ok(out)
}

fn line_block(body: &[u8], version: u8) -> Option<Stroke> {
    let mut c = Cur { b: body, p: 0 };
    c.id(1)?; // parent
    c.id(2)?; // item
    c.id(3)?; // left
    c.id(4)?; // right
    let deleted = c.int(5)?;
    if deleted > 0 { return None; }
    let mut v = c.sub(6)?;
    if v.u8()? != ITEM_LINE { return None; }
    let tool = v.int(1)?;
    let _color = v.int(2)?;
    let _thickness = v.double(3)?;
    let _start = v.float(4)?;
    let mut pts = v.sub(5)?;
    if ERASERS.contains(&tool) { return None; }
    let size = if version >= 2 { 14 } else { 24 };
    let mut points = Vec::with_capacity(pts.left() / size);
    while pts.left() >= size {
        let x = pts.f32()?;
        let y = pts.f32()?;
        if version >= 2 {
            let _speed = pts.u16()?;
            let width = pts.u16()? as f32 / 4.0;
            let _direction = pts.u8()?;
            let _pressure = pts.u8()?;
            points.push((x, y, width));
        } else {
            let _speed = pts.f32()?;
            let _direction = pts.f32()?;
            let width = pts.f32()?;
            let _pressure = pts.f32()?;
            points.push((x, y, width));
        }
    }
    (!points.is_empty()).then_some(Stroke { tool, points })
}

const BLOCK_ROOT_TEXT: u8 = 0x07;

#[derive(Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Debug)]
enum Node { Start, Id(u8, u64), End }

struct Ch { id: (u8, u64), left: (u8, u64), right: (u8, u64), c: Option<char> } // c None = deleted or format code

/// Text typed on a page (the root text), one entry per paragraph. Mirrors rmscene's
/// expand_text_items + toposort_items: each item holds a run of characters with consecutive ids,
/// ordered by left/right neighbours; deleted characters and format codes are dropped.
pub fn read_text(data: &[u8]) -> Result<Vec<String>, String> {
    if !data.starts_with(HEADER) { return Err("not a v6 .rm file".into()); }
    let mut c = Cur { b: data, p: 43 };
    let mut chars: Vec<Ch> = Vec::new();
    while c.left() >= 8 {
        let len = c.u32().ok_or("truncated")? as usize;
        let _ = (c.u8(), c.u8(), c.u8());
        let kind = c.u8().ok_or("truncated")?;
        let body = c.bytes(len).ok_or("truncated block")?;
        if kind == BLOCK_ROOT_TEXT { text_block(body, &mut chars).ok_or("bad text block")?; }
    }
    let order = toposort(&chars);
    let mut text = String::new();
    for i in order { if let Some(ch) = chars[i].c { text.push(ch); } }
    Ok(text.split('\n').map(str::to_owned).collect())
}

fn text_block(body: &[u8], out: &mut Vec<Ch>) -> Option<()> {
    let mut c = Cur { b: body, p: 0 };
    c.id(1)?;
    let mut s2 = c.sub(2)?;
    let mut a = s2.sub(1)?;
    let mut b = a.sub(1)?;
    let n = b.varuint()?;
    for _ in 0..n {
        let mut it = b.sub(0)?;
        let id = it.id(2)?;
        let left = it.id(3)?;
        let right = it.id(4)?;
        let deleted = it.int(5)? as u64;
        let mut value: Option<String> = None; // None = format code
        if let Some(mut s6) = it.sub(6) {
            let len = s6.varuint()? as usize;
            let _ascii = s6.u8()?;
            let s = std::str::from_utf8(s6.bytes(len)?).ok()?.to_owned();
            value = if s6.tag(2, 0x4).is_some() { None } else { Some(s) };
        } else if deleted == 0 {
            value = Some(String::new());
        }
        // expand to one entry per character with consecutive ids
        let run: Vec<Option<char>> = if deleted > 0 {
            vec![None; deleted as usize]
        } else {
            match &value { Some(s) => s.chars().map(Some).collect(), None => vec![None] }
        };
        if run.is_empty() { continue; }
        let (mut cid, mut cleft) = (id, left);
        for (k, ch) in run.iter().enumerate() {
            let last = k + 1 == run.len();
            let cright = if last { right } else { (cid.0, cid.1 + 1) };
            out.push(Ch { id: cid, left: cleft, right: cright, c: *ch });
            cleft = cid;
            cid = (cid.0, cid.1 + 1);
        }
    }
    Some(())
}

fn toposort(chars: &[Ch]) -> Vec<usize> {
    use std::cmp::Reverse;
    use std::collections::{BinaryHeap, HashMap};
    let index: HashMap<(u8, u64), usize> = chars.iter().enumerate().map(|(i, c)| (c.id, i)).collect();
    let side = |id: (u8, u64), left: bool| {
        if id == (0, 0) || !index.contains_key(&id) { if left { Node::Start } else { Node::End } } else { Node::Id(id.0, id.1) }
    };
    let mut indeg: HashMap<Node, usize> = HashMap::new();
    let mut deps: HashMap<Node, Vec<Node>> = HashMap::new();
    indeg.insert(Node::Start, 0);
    indeg.insert(Node::End, 0);
    for c in chars {
        let me = Node::Id(c.id.0, c.id.1);
        let (l, r) = (side(c.left, true), side(c.right, false));
        indeg.entry(l).or_insert(0);
        *indeg.entry(me).or_insert(0) += 1;
        deps.entry(l).or_default().push(me);
        *indeg.entry(r).or_insert(0) += 1;
        deps.entry(me).or_default().push(r);
    }
    // ties: higher author id first, then lower counter (as rmscene)
    let key = |n: Node| match n { Node::Start => (0u8, 0i64, 0u64), Node::Id(a, b) => (1, -(a as i64), b), Node::End => (2, 0, 0) };
    let mut ready: BinaryHeap<Reverse<((u8, i64, u64), Node)>> =
        indeg.iter().filter(|(_, d)| **d == 0).map(|(n, _)| Reverse((key(*n), *n))).collect();
    let mut out = Vec::with_capacity(chars.len());
    while let Some(Reverse((_, n))) = ready.pop() {
        if let Node::Id(a, b) = n { if let Some(&i) = index.get(&(a, b)) { out.push(i); } }
        if let Some(ds) = deps.get(&n) {
            for d in ds {
                let e = indeg.get_mut(d).unwrap();
                *e -= 1;
                if *e == 0 { ready.push(Reverse((key(*d), *d))); }
            }
        }
    }
    out
}
