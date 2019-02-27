#![allow(unknown_lints)]
#![warn(clippy::all)]
#![allow(
    clippy::too_many_arguments,
    clippy::cast_lossless,
    clippy::many_single_char_names
)]

use byteorder::{BigEndian as BE, ByteOrder};
use std::ops::Deref;

#[derive(Copy, Clone, Debug)]
pub struct FontInfo<Data: Deref<Target = [u8]>> {
    data: Data, // pointer to .ttf file
    // fontstart: usize,       // offset of start of font
    num_glyphs: u32, // number of glyphs, needed for range checking
    loca: u32,
    head: u32,
    glyf: u32,
    hhea: u32,
    hmtx: u32,
    name: u32,
    kern: u32,                // table locations as offset from start of .ttf
    index_map: u32,           // a cmap mapping for our chosen character encoding
    index_to_loc_format: u32, // format needed to map from glyph index to glyph
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
#[repr(C)]
pub struct Vertex {
    pub x: i16,
    pub y: i16,
    pub cx: i16,
    pub cy: i16,
    type_: u8,
}

impl Vertex {
    pub fn vertex_type(&self) -> VertexType {
        match self.type_ {
            1 => VertexType::MoveTo,
            2 => VertexType::LineTo,
            3 => VertexType::CurveTo,
            type_ => panic!("Invalid vertex type: {}", type_),
        }
    }
}

#[test]
fn test_vertex_type() {
    fn v(type_: VertexType) -> Vertex {
        Vertex {
            x: 0,
            y: 0,
            cx: 0,
            cy: 0,
            type_: type_ as u8,
        }
    }
    assert_eq!(v(VertexType::MoveTo).vertex_type(), VertexType::MoveTo);
    assert_eq!(v(VertexType::LineTo).vertex_type(), VertexType::LineTo);
    assert_eq!(v(VertexType::CurveTo).vertex_type(), VertexType::CurveTo);
}

#[test]
#[should_panic]
fn test_invalid_vertex_type() {
    let v = Vertex {
        x: 0,
        y: 0,
        cx: 0,
        cy: 0,
        type_: 255,
    };
    let s = match v.vertex_type() {
        VertexType::MoveTo => "move to",
        VertexType::LineTo => "line to",
        VertexType::CurveTo => "curve to",
    };
    // With `Vertex::vertex_type` defined as `transmute` this would be undefined
    // behavior:
    println!("{}", s);
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum VertexType {
    MoveTo = 1,
    LineTo = 2,
    CurveTo = 3,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct Rect<T> {
    pub x0: T,
    pub y0: T,
    pub x1: T,
    pub y1: T,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct HMetrics {
    pub advance_width: i32,
    pub left_side_bearing: i32,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct VMetrics {
    pub ascent: i32,
    pub descent: i32,
    pub line_gap: i32,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(C)]
pub enum PlatformId {
    // platformID
    Unicode = 0,
    Mac = 1,
    Iso = 2,
    Microsoft = 3,
}
fn platform_id(v: u16) -> Option<PlatformId> {
    use crate::PlatformId::*;
    match v {
        0 => Some(Unicode),
        1 => Some(Mac),
        2 => Some(Iso),
        3 => Some(Microsoft),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(C)]
#[allow(non_camel_case_types)]
pub enum UnicodeEid {
    // encodingID for PLATFORM_ID_UNICODE
    Unicode_1_0 = 0,
    Unicode_1_1 = 1,
    Iso_10646 = 2,
    Unicode_2_0_Bmp = 3,
    Unicode_2_0_Full = 4,
}
fn unicode_eid(v: u16) -> Option<UnicodeEid> {
    use crate::UnicodeEid::*;
    match v {
        0 => Some(Unicode_1_0),
        1 => Some(Unicode_1_1),
        2 => Some(Iso_10646),
        3 => Some(Unicode_2_0_Bmp),
        4 => Some(Unicode_2_0_Full),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(C)]
pub enum MicrosoftEid {
    // encodingID for PLATFORM_ID_MICROSOFT
    Symbol = 0,
    UnicodeBMP = 1,
    Shiftjis = 2,
    UnicodeFull = 10,
}
fn microsoft_eid(v: u16) -> Option<MicrosoftEid> {
    use crate::MicrosoftEid::*;
    match v {
        0 => Some(Symbol),
        1 => Some(UnicodeBMP),
        2 => Some(Shiftjis),
        10 => Some(UnicodeFull),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(C)]
pub enum MacEid {
    // encodingID for PLATFORM_ID_MAC; same as Script Manager codes
    Roman = 0,
    Arabic = 4,
    Japanese = 1,
    Hebrew = 5,
    ChineseTrad = 2,
    Greek = 6,
    Korean = 3,
    Russian = 7,
}
fn mac_eid(v: u16) -> Option<MacEid> {
    use crate::MacEid::*;
    match v {
        0 => Some(Roman),
        1 => Some(Japanese),
        2 => Some(ChineseTrad),
        3 => Some(Korean),
        4 => Some(Arabic),
        5 => Some(Hebrew),
        6 => Some(Greek),
        7 => Some(Russian),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(C)]
pub enum MicrosoftLang {
    // languageID for PLATFORM_ID_MICROSOFT; same as LCID...
    // problematic because there are e.g. 16 english LCIDs and 16 arabic LCIDs
    English = 0x0409,
    Italian = 0x0410,
    Chinese = 0x0804,
    Japanese = 0x0411,
    Dutch = 0x0413,
    Korean = 0x0412,
    French = 0x040c,
    Russian = 0x0419,
    German = 0x0407,
    // Spanish = 0x0409,
    Hebrew = 0x040d,
    Swedish = 0x041D,
}
fn microsoft_lang(v: u16) -> Option<MicrosoftLang> {
    use crate::MicrosoftLang::*;
    match v {
        0x0409 => Some(English),
        0x0804 => Some(Chinese),
        0x0413 => Some(Dutch),
        0x040c => Some(French),
        0x0407 => Some(German),
        0x040d => Some(Hebrew),
        0x0410 => Some(Italian),
        0x0411 => Some(Japanese),
        0x0412 => Some(Korean),
        0x0419 => Some(Russian),
        0x041D => Some(Swedish),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(C)]
pub enum MacLang {
    // languageID for PLATFORM_ID_MAC
    English = 0,
    Japanese = 11,
    Arabic = 12,
    Korean = 23,
    Dutch = 4,
    Russian = 32,
    French = 1,
    Spanish = 6,
    German = 2,
    Swedish = 5,
    Hebrew = 10,
    ChineseSimplified = 33,
    Italian = 3,
    ChineseTrad = 19,
}
fn mac_lang(v: u16) -> Option<MacLang> {
    use crate::MacLang::*;
    match v {
        0 => Some(English),
        12 => Some(Arabic),
        4 => Some(Dutch),
        1 => Some(French),
        2 => Some(German),
        10 => Some(Hebrew),
        3 => Some(Italian),
        11 => Some(Japanese),
        23 => Some(Korean),
        32 => Some(Russian),
        6 => Some(Spanish),
        5 => Some(Swedish),
        33 => Some(ChineseSimplified),
        19 => Some(ChineseTrad),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum PlatformEncodingLanguageId {
    Unicode(Option<Result<UnicodeEid, u16>>, Option<u16>),
    Mac(Option<Result<MacEid, u16>>, Option<Result<MacLang, u16>>),
    Iso(Option<u16>, Option<u16>),
    Microsoft(
        Option<Result<MicrosoftEid, u16>>,
        Option<Result<MicrosoftLang, u16>>,
    ),
}
fn platform_encoding_id(
    platform_id: PlatformId,
    encoding_id: Option<u16>,
    language_id: Option<u16>,
) -> PlatformEncodingLanguageId {
    match platform_id {
        PlatformId::Unicode => PlatformEncodingLanguageId::Unicode(
            encoding_id.map(|id| unicode_eid(id).ok_or(id)),
            language_id,
        ),
        PlatformId::Mac => PlatformEncodingLanguageId::Mac(
            encoding_id.map(|id| mac_eid(id).ok_or(id)),
            language_id.map(|id| mac_lang(id).ok_or(id)),
        ),
        PlatformId::Iso => PlatformEncodingLanguageId::Iso(encoding_id, language_id),
        PlatformId::Microsoft => PlatformEncodingLanguageId::Microsoft(
            encoding_id.map(|id| microsoft_eid(id).ok_or(id)),
            language_id.map(|id| microsoft_lang(id).ok_or(id)),
        ),
    }
}

// # accessors to parse data from file

// on platforms that don't allow misaligned reads, if we want to allow
// truetype fonts that aren't padded to alignment, define
// ALLOW_UNALIGNED_TRUETYPE

/// Return `true` if `data` holds a font stored in a format this crate
/// recognizes, according to its signature in the initial bytes.
pub fn is_font(data: &[u8]) -> bool {
    if data.len() >= 4 {
        let tag = &data[0..4];
        tag == [b'1', 0, 0, 0] || tag == b"typ1" || tag == b"OTTO" || tag == [0, 1, 0, 0]
    } else {
        false
    }
}

/// Return `true` if `data` holds a TrueType Collection, according to its
/// signature in the initial bytes. A TrueType Collection stores several fonts
/// in a single file, allowing them to share data for glyphs they have in
/// common.
pub fn is_collection(data: &[u8]) -> bool {
    data.len() >= 4 && &data[0..4] == b"ttcf"
}

fn find_table(data: &[u8], fontstart: usize, tag: &[u8]) -> u32 {
    let num_tables = BE::read_u16(&data[fontstart + 4..]);
    let tabledir = fontstart + 12;
    for i in 0..num_tables {
        let loc = tabledir + 16 * (i as usize);
        if &data[loc..loc + 4] == tag {
            return BE::read_u32(&data[loc + 8..]);
        }
    }
    0
}

/// Each .ttf/.ttc file may have more than one font. Each font has a sequential
/// index number starting from 0. Call this function to get the font offset for
/// a given index; it returns None if the index is out of range. A regular .ttf
/// file will only define one font and it always be at offset 0, so it will
/// return Some(0) for index 0, and None for all other indices. You can just
/// skip this step if you know it's that kind of font.
pub fn get_font_offset_for_index(font_collection: &[u8], index: i32) -> Option<u32> {
    // if it's just a font, there's only one valid index
    if is_font(font_collection) {
        return if index == 0 { Some(0) } else { None };
    }
    // check if it's a TTC
    if is_collection(font_collection)
        && (BE::read_u32(&font_collection[4..]) == 0x0001_0000
            || BE::read_u32(&font_collection[4..]) == 0x0002_0000)
    {
        let n = BE::read_i32(&font_collection[8..]);
        if index >= n {
            return None;
        }
        return Some(BE::read_u32(&font_collection[12 + (index as usize) * 4..]));
    }
    None
}

macro_rules! read_ints {
    ($n:expr, i16, $data:expr) => {{
        let mut nums = [0; $n];
        let data = $data;
        BE::read_i16_into(&data[..$n * 2], &mut nums);
        nums
    }};
    ($n:expr, u16, $data:expr) => {{
        let mut nums = [0; $n];
        let data = $data;
        BE::read_u16_into(&data[..$n * 2], &mut nums);
        nums
    }};
    ($n:expr, u32, $data:expr) => {{
        let mut nums = [0; $n];
        let data = $data;
        BE::read_u32_into(&data[..$n * 4], &mut nums);
        nums
    }};
}

impl<Data: Deref<Target = [u8]>> FontInfo<Data> {
    /// Given an offset into the file that defines a font, this function builds
    /// the necessary cached info for the rest of the system.
    pub fn new(data: Data, fontstart: usize) -> Option<FontInfo<Data>> {
        let cmap = find_table(&data, fontstart, b"cmap"); // required
        let loca = find_table(&data, fontstart, b"loca"); // required
        let head = find_table(&data, fontstart, b"head"); // required
        let glyf = find_table(&data, fontstart, b"glyf"); // required
        let hhea = find_table(&data, fontstart, b"hhea"); // required
        let hmtx = find_table(&data, fontstart, b"hmtx"); // required
        let name = find_table(&data, fontstart, b"name"); // not required
        let kern = find_table(&data, fontstart, b"kern"); // not required
        if cmap == 0 || loca == 0 || head == 0 || glyf == 0 || hhea == 0 || hmtx == 0 {
            return None;
        }
        let t = find_table(&data, fontstart, b"maxp");
        let num_glyphs = if t != 0 {
            BE::read_u16(&data[t as usize + 4..])
        } else {
            0xffff
        };

        // find a cmap encoding table we understand *now* to avoid searching
        // later. (todo: could make this installable)
        // the same regardless of glyph.
        let num_tables = BE::read_u16(&data[cmap as usize + 2..]);
        let mut index_map = 0;
        for i in 0..num_tables {
            let encoding_record = (cmap + 4 + 8 * (i as u32)) as usize;
            // find an encoding we understand:
            match platform_id(BE::read_u16(&data[encoding_record..])) {
                Some(PlatformId::Microsoft) => {
                    match microsoft_eid(BE::read_u16(&data[encoding_record + 2..])) {
                        Some(MicrosoftEid::UnicodeBMP) | Some(MicrosoftEid::UnicodeFull) => {
                            // MS/Unicode
                            index_map = cmap + BE::read_u32(&data[encoding_record + 4..]);
                        }
                        _ => (),
                    }
                }
                Some(PlatformId::Unicode) => {
                    // Mac/iOS has these
                    // all the encodingIDs are unicode, so we don't bother to check it
                    index_map = cmap + BE::read_u32(&data[encoding_record + 4..]);
                }
                _ => (),
            }
        }
        if index_map == 0 {
            return None;
        }
        let index_to_loc_format = BE::read_u16(&data[head as usize + 50..]) as u32;
        Some(FontInfo {
            // fontstart: fontstart,
            data,
            loca,
            head,
            glyf,
            hhea,
            hmtx,
            name,
            kern,
            num_glyphs: num_glyphs as u32,
            index_map,
            index_to_loc_format,
        })
    }

    pub fn get_num_glyphs(&self) -> u32 {
        self.num_glyphs
    }

    /// If you're going to perform multiple operations on the same character
    /// and you want a speed-up, call this function with the character you're
    /// going to process, then use glyph-based functions instead of the
    /// codepoint-based functions.
    pub fn find_glyph_index(&self, unicode_codepoint: u32) -> u32 {
        let data = &self.data;
        let index_map = &data[self.index_map as usize..]; //self.index_map as usize;

        let format = BE::read_u16(index_map);
        match format {
            0 => {
                // apple byte encoding
                let bytes = BE::read_u16(&index_map[2..]);
                if unicode_codepoint < bytes as u32 - 6 {
                    return index_map[6 + unicode_codepoint as usize] as u32;
                }
                0
            }
            6 => {
                let first = BE::read_u16(&index_map[6..]) as u32;
                let count = BE::read_u16(&index_map[8..]) as u32;
                if unicode_codepoint >= first && unicode_codepoint < first + count {
                    return BE::read_u16(&index_map[10 + (unicode_codepoint - first) as usize * 2..])
                        as u32;
                }
                0
            }
            2 => {
                // @TODO: high-byte mapping for japanese/chinese/korean
                panic!("Index map format unsupported: 2");
            }
            4 => {
                // standard mapping for windows fonts: binary search collection of ranges
                let segcount = BE::read_u16(&index_map[6..]) as usize >> 1;
                let mut search_range = BE::read_u16(&index_map[8..]) as usize >> 1;
                let mut entry_selector = BE::read_u16(&index_map[10..]);
                let range_shift = BE::read_u16(&index_map[12..]) as usize >> 1;

                // do a binary search of the segments
                let end_count = self.index_map as usize + 14;
                let mut search = end_count;

                if unicode_codepoint > 0xffff {
                    return 0;
                }

                // they lie from endCount .. endCount + segCount
                // but searchRange is the nearest power of two, so...
                if unicode_codepoint >= BE::read_u16(&data[search + range_shift * 2..]) as u32 {
                    search += range_shift * 2;
                }

                // now decrement to bias correctly to find smallest
                search -= 2;
                while entry_selector != 0 {
                    search_range >>= 1;
                    let end = BE::read_u16(&data[search + search_range * 2..]) as u32;
                    if unicode_codepoint > end {
                        search += search_range * 2;
                    }
                    entry_selector -= 1;
                }
                search += 2;

                {
                    let item = (search - end_count) >> 1;
                    assert!(
                        unicode_codepoint <= BE::read_u16(&data[end_count + 2 * item..]) as u32
                    );
                    let start = BE::read_u16(&index_map[14 + segcount * 2 + 2 + 2 * item..]) as u32;
                    if unicode_codepoint < start {
                        return 0;
                    }
                    let offset =
                        BE::read_u16(&index_map[14 + segcount * 6 + 2 + 2 * item..]) as usize;
                    if offset == 0 {
                        return (unicode_codepoint as i32
                            + BE::read_i16(&index_map[14 + segcount * 4 + 2 + 2 * item..]) as i32)
                            as u16 as u32;
                    }
                    BE::read_u16(
                        &index_map[offset
                            + (unicode_codepoint - start) as usize * 2
                            + 14
                            + segcount * 6
                            + 2
                            + 2 * item..],
                    ) as u32
                }
            }
            12 | 13 => {
                let mut low = 0u32;
                let mut high = BE::read_u32(&index_map[12..]);
                let groups = &index_map[16..];

                // Binary search of the right group
                while low < high {
                    let mid = (low + high) / 2; // rounds down, so low <= mid < high
                    let mid12 = (mid * 12) as usize;
                    let group = &groups[mid12..mid12 + 12];
                    let start_char = BE::read_u32(group);
                    if unicode_codepoint < start_char {
                        high = mid;
                    } else if unicode_codepoint > BE::read_u32(&group[4..]) {
                        low = mid + 1;
                    } else {
                        let start_glyph = BE::read_u32(&group[8..]);
                        if format == 12 {
                            return start_glyph + unicode_codepoint - start_char;
                        } else {
                            return start_glyph;
                        }
                    }
                }

                0
            }
            n => panic!("Index map format unsupported: {}", n),
        }
    }

    /// Returns the series of vertices encoding the shape of the glyph for this
    /// codepoint.
    ///
    /// The shape is a series of countours. Each one starts with
    /// a moveto, then consists of a series of mixed
    /// lineto and curveto segments. A lineto
    /// draws a line from previous endpoint to its x,y; a curveto
    /// draws a quadratic bezier from previous endpoint to
    /// its x,y, using cx,cy as the bezier control point.
    pub fn get_codepoint_shape(&self, unicode_codepoint: u32) -> Option<Vec<Vertex>> {
        self.get_glyph_shape(self.find_glyph_index(unicode_codepoint))
    }

    fn get_glyf_offset(&self, glyph_index: u32) -> Option<u32> {
        if glyph_index >= self.num_glyphs || self.index_to_loc_format >= 2 {
            // glyph index out of range or unknown index->glyph map format
            return None;
        }

        let [g1, g2] = if self.index_to_loc_format == 0 {
            let d = &self.data[(self.loca + glyph_index * 2) as usize..];
            let [g1, g2] = read_ints!(2, u16, d);
            [g1 as u32 * 2, g2 as u32 * 2]
        } else {
            read_ints!(2, u32, &self.data[(self.loca + glyph_index * 4) as usize..])
        };
        if g1 == g2 {
            None
        } else {
            Some(self.glyf + g1)
        }
    }

    /// Like `get_codepoint_box`, but takes a glyph index. Use this if you have
    /// cached the glyph index for a codepoint.
    pub fn get_glyph_box(&self, glyph_index: u32) -> Option<Rect<i16>> {
        let g = self.get_glyf_offset(glyph_index)? as usize;
        let [x0, y0, x1, y1] = read_ints!(4, i16, &self.data[g + 2..]);
        Some(Rect { x0, y0, x1, y1 })
    }

    /// Gets the bounding box of the visible part of the glyph, in unscaled
    /// coordinates
    pub fn get_codepoint_box(&self, codepoint: u32) -> Option<Rect<i16>> {
        self.get_glyph_box(self.find_glyph_index(codepoint))
    }

    /// returns true if nothing is drawn for this glyph
    pub fn is_glyph_empty(&self, glyph_index: u32) -> bool {
        match self.get_glyf_offset(glyph_index) {
            Some(g) => {
                let number_of_contours = BE::read_i16(&self.data[g as usize..]);
                number_of_contours == 0
            }
            None => true,
        }
    }

    /// Like `get_codepoint_shape`, but takes a glyph index instead. Use this
    /// if you have cached the glyph index for a codepoint.
    pub fn get_glyph_shape(&self, glyph_index: u32) -> Option<Vec<Vertex>> {
        let g = match self.get_glyf_offset(glyph_index) {
            Some(g) => &self.data[g as usize..],
            None => return None,
        };

        let number_of_contours = BE::read_i16(g);
        let vertices: Vec<Vertex> = if number_of_contours > 0 {
            self.glyph_shape_positive_contours(g, number_of_contours as usize)
        } else if number_of_contours == -1 {
            // Compound shapes
            let mut more = true;
            let mut comp = &g[10..];
            let mut vertices = Vec::new();
            while more {
                let mut mtx = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0];

                let [flags, gidx] = read_ints!(2, i16, comp);
                comp = &comp[4..];
                let gidx = gidx as u16;

                if flags & 2 != 0 {
                    // XY values
                    if flags & 1 != 0 {
                        // shorts
                        let [a, b] = read_ints!(2, i16, comp);
                        comp = &comp[4..];
                        mtx[4] = a as f32;
                        mtx[5] = b as f32;
                    } else {
                        mtx[4] = (comp[0] as i8) as f32;
                        mtx[5] = (comp[1] as i8) as f32;
                        comp = &comp[2..];
                    }
                } else {
                    panic!("Matching points not supported.");
                }
                if flags & (1 << 3) != 0 {
                    // WE_HAVE_A_SCALE
                    mtx[0] = BE::read_i16(comp) as f32 / 16384.0;
                    comp = &comp[2..];
                    mtx[1] = 0.0;
                    mtx[2] = 0.0;
                    mtx[3] = mtx[0];
                } else if flags & (1 << 6) != 0 {
                    // WE_HAVE_AN_X_AND_YSCALE
                    let [a, b] = read_ints!(2, i16, comp);
                    comp = &comp[4..];
                    mtx[0] = a as f32 / 16384.0;
                    mtx[1] = 0.0;
                    mtx[2] = 0.0;
                    mtx[3] = b as f32 / 16384.0;
                } else if flags & (1 << 7) != 0 {
                    // WE_HAVE_A_TWO_BY_TWO
                    let [a, b, c, d] = read_ints!(4, i16, comp);
                    comp = &comp[8..];
                    mtx[0] = a as f32 / 16384.0;
                    mtx[1] = b as f32 / 16384.0;
                    mtx[2] = c as f32 / 16384.0;
                    mtx[3] = d as f32 / 16384.0;
                }

                // Find transformation scales.
                let m = (mtx[0] * mtx[0] + mtx[1] * mtx[1]).sqrt();
                let n = (mtx[2] * mtx[2] + mtx[3] * mtx[3]).sqrt();

                // Get indexed glyph.
                let mut comp_verts = self.get_glyph_shape(gidx as u32).unwrap_or_else(Vec::new);
                if !comp_verts.is_empty() {
                    // Transform vertices
                    for v in &mut *comp_verts {
                        let (x, y, cx, cy) = (v.x as f32, v.y as f32, v.cx as f32, v.cy as f32);
                        *v = Vertex {
                            type_: v.type_,
                            x: (m * (mtx[0] * x + mtx[2] * y + mtx[4])) as i16,
                            y: (n * (mtx[1] * x + mtx[3] * y + mtx[5])) as i16,
                            cx: (m * (mtx[0] * cx + mtx[2] * cy + mtx[4])) as i16,
                            cy: (n * (mtx[1] * cx + mtx[3] * cy + mtx[5])) as i16,
                        };
                    }
                    // Append vertices.
                    vertices.append(&mut comp_verts);
                }
                // More components ?
                more = flags & (1 << 5) != 0;
            }
            vertices
        } else if number_of_contours < 0 {
            panic!("Contour format not supported.")
        } else {
            return None;
        };
        Some(vertices)
    }

    #[inline]
    fn glyph_shape_positive_contours(
        &self,
        glyph_data: &[u8],
        number_of_contours: usize,
    ) -> Vec<Vertex> {
        use crate::VertexType::*;

        struct FlagData {
            flags: u8,
            x: i16,
            y: i16,
        }

        #[inline]
        fn close_shape(
            vertices: &mut Vec<Vertex>,
            was_off: bool,
            start_off: bool,
            sx: i16,
            sy: i16,
            scx: i16,
            scy: i16,
            cx: i16,
            cy: i16,
        ) {
            if start_off {
                if was_off {
                    vertices.push(Vertex {
                        type_: CurveTo as u8,
                        x: (cx + scx) >> 1,
                        y: (cy + scy) >> 1,
                        cx,
                        cy,
                    });
                }
                vertices.push(Vertex {
                    type_: CurveTo as u8,
                    x: sx,
                    y: sy,
                    cx: scx,
                    cy: scy,
                });
            } else {
                vertices.push(if was_off {
                    Vertex {
                        type_: CurveTo as u8,
                        x: sx,
                        y: sy,
                        cx,
                        cy,
                    }
                } else {
                    Vertex {
                        type_: LineTo as u8,
                        x: sx,
                        y: sy,
                        cx: 0,
                        cy: 0,
                    }
                });
            }
        }

        let number_of_contours = number_of_contours as usize;
        let mut start_off = false;
        let mut was_off = false;
        let end_points_of_contours = &glyph_data[10..];
        let ins = BE::read_u16(&glyph_data[10 + number_of_contours * 2..]) as usize;
        let mut points = &glyph_data[10 + number_of_contours * 2 + 2 + ins..];

        let n = 1 + BE::read_u16(&end_points_of_contours[number_of_contours * 2 - 2..]) as usize;

        let m = n + 2 * number_of_contours; // a loose bound on how many vertices we might need
        let mut vertices: Vec<Vertex> = Vec::with_capacity(m);

        let mut flag_data = Vec::with_capacity(n);

        let mut next_move = 0;

        // in first pass, we load uninterpreted data into the allocated array above

        // first load flags
        {
            let mut flagcount = 0;
            let mut flags = 0;
            for _ in 0..n {
                if flagcount == 0 {
                    flags = points[0];
                    if flags & 8 != 0 {
                        flagcount = points[1];
                        points = &points[2..];
                    } else {
                        points = &points[1..];
                    }
                } else {
                    flagcount -= 1;
                }
                flag_data.push(FlagData { flags, x: 0, y: 0 });
            }
        }

        // now load x coordinates
        let mut x_coord = 0_i16;
        for flag_data in &mut flag_data {
            let flags = flag_data.flags;
            if flags & 2 != 0 {
                let dx = i16::from(points[0]);
                points = &points[1..];
                if flags & 16 != 0 {
                    // ???
                    x_coord += dx;
                } else {
                    x_coord -= dx;
                }
            } else if flags & 16 == 0 {
                x_coord += BE::read_i16(points);
                points = &points[2..];
            }
            flag_data.x = x_coord;
        }

        // now load y coordinates
        let mut y_coord = 0_i16;
        for flag_data in &mut flag_data {
            let flags = flag_data.flags;
            if flags & 4 != 0 {
                let dy = i16::from(points[0]);
                points = &points[1..];
                if flags & 32 != 0 {
                    y_coord += dy;
                } else {
                    y_coord -= dy;
                }
            } else if flags & 32 == 0 {
                y_coord += BE::read_i16(points);
                points = &points[2..];
            }
            flag_data.y = y_coord;
        }

        // now convert them to our format
        let mut sx = 0;
        let mut sy = 0;
        let mut cx = 0;
        let mut cy = 0;
        let mut scx = 0;
        let mut scy = 0;
        let mut j = 0;

        let mut iter = flag_data.into_iter().enumerate().peekable();

        while let Some((index, FlagData { flags, x, y })) = iter.next() {
            if next_move == index {
                if index != 0 {
                    close_shape(&mut vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);
                }

                // now start the new one
                start_off = flags & 1 == 0;
                if start_off {
                    // if we start off with an off-curve point, then when we need to find a
                    // point on the curve where we can start, and we
                    // need to save some state for
                    // when we wraparound.
                    scx = x;
                    scy = y;

                    let (next_flags, next_x, next_y) = match iter.peek() {
                        Some((_, fd)) => (fd.flags, fd.x, fd.y),
                        None => break,
                    };

                    if next_flags & 1 == 0 {
                        // next point is also a curve point, so interpolate an on-point curve
                        sx = (x + next_x) >> 1;
                        sy = (y + next_y) >> 1;
                    } else {
                        // otherwise just use the next point as our start point
                        sx = next_x;
                        sy = next_y;

                        // we're using point i+1 as the starting point, so skip it
                        let _ = iter.next();
                    }
                } else {
                    sx = x;
                    sy = y;
                }
                vertices.push(Vertex {
                    type_: MoveTo as u8,
                    x: sx,
                    y: sy,
                    cx: 0,
                    cy: 0,
                });
                was_off = false;
                next_move = 1 + BE::read_u16(&end_points_of_contours[j * 2..]) as usize;
                j += 1;
            } else if flags & 1 == 0 {
                // if it's a curve
                if was_off {
                    // two off-curve control points in a row means interpolate an on-curve
                    // midpoint
                    vertices.push(Vertex {
                        type_: CurveTo as u8,
                        x: ((cx + x) >> 1),
                        y: ((cy + y) >> 1),
                        cx,
                        cy,
                    });
                }
                cx = x;
                cy = y;
                was_off = true;
            } else {
                vertices.push(if was_off {
                    Vertex {
                        type_: CurveTo as u8,
                        x,
                        y,
                        cx,
                        cy,
                    }
                } else {
                    Vertex {
                        type_: LineTo as u8,
                        x,
                        y,
                        cx: 0,
                        cy: 0,
                    }
                });
                was_off = false;
            }
        }
        close_shape(
            &mut vertices,
            // &mut num_vertices,
            was_off,
            start_off,
            sx,
            sy,
            scx,
            scy,
            cx,
            cy,
        );

        vertices
    }

    /// like `get_codepoint_h_metrics`, but takes a glyph index instead. Use
    /// this if you have cached the glyph index for a codepoint.
    pub fn get_glyph_h_metrics(&self, glyph_index: u32) -> HMetrics {
        let num_of_long_hor_metrics = BE::read_u16(&self.data[self.hhea as usize + 34..]) as usize;
        let glyph_index = glyph_index as usize;
        if glyph_index < num_of_long_hor_metrics {
            let data = &self.data[self.hmtx as usize + 4 * glyph_index..];
            let [advance_width, left_side_bearing] = read_ints!(2, i16, data);
            HMetrics {
                advance_width: i32::from(advance_width),
                left_side_bearing: i32::from(left_side_bearing),
            }
        } else {
            HMetrics {
                advance_width: BE::read_i16(
                    &self.data[self.hmtx as usize + 4 * (num_of_long_hor_metrics - 1)..],
                ) as i32,
                left_side_bearing: BE::read_i16(
                    &self.data[self.hmtx as usize
                        + 4 * num_of_long_hor_metrics
                        + 2 * (glyph_index as isize - num_of_long_hor_metrics as isize) as usize..],
                ) as i32,
            }
        }
    }

    /// like `get_codepoint_kern_advance`, but takes glyph indices instead. Use
    /// this if you have cached the glyph indices for the codepoints.
    pub fn get_glyph_kern_advance(&self, glyph_1: u32, glyph_2: u32) -> i32 {
        let kern = &self.data[self.kern as usize..];
        // we only look at the first table. it must be 'horizontal' and format 0
        if self.kern == 0 || BE::read_u16(&kern[2..]) < 1 || BE::read_u16(&kern[8..]) != 1 {
            // kern not present, OR
            // no tables (need at least one), OR
            // horizontal flag not set in format
            return 0;
        }

        let mut l: i32 = 0;
        let mut r: i32 = BE::read_u16(&kern[10..]) as i32 - 1;
        let needle = glyph_1 << 16 | glyph_2;
        while l <= r {
            let m = (l + r) >> 1;
            let straw = BE::read_u32(&kern[18 + (m as usize) * 6..]); // note: unaligned read
            if needle < straw {
                r = m - 1;
            } else if needle > straw {
                l = m + 1;
            } else {
                return BE::read_i16(&kern[22 + (m as usize) * 6..]) as i32;
            }
        }
        0
    }

    /// an additional amount to add to the 'advance' value between cp1 and cp2
    pub fn get_codepoint_kern_advance(&self, cp1: u32, cp2: u32) -> i32 {
        if self.kern == 0 {
            // if no kerning table, don't waste time looking up both codepoint->glyphs
            0
        } else {
            self.get_glyph_kern_advance(self.find_glyph_index(cp1), self.find_glyph_index(cp2))
        }
    }

    /// `left_side_bearing` is the offset from the current horizontal position
    /// to the left edge of the character `advance_width` is the offset
    /// from the current horizontal position to the next horizontal
    /// position these are
    /// expressed in unscaled
    /// coordinates
    pub fn get_codepoint_h_metrics(&self, codepoint: u32) -> HMetrics {
        self.get_glyph_h_metrics(self.find_glyph_index(codepoint))
    }

    /// `ascent` is the coordinate above the baseline the font extends; descent
    /// is the coordinate below the baseline the font extends (i.e. it is
    /// typically negative) `line_gap` is the spacing between one row's
    /// descent and the next row's ascent... so you should advance the
    /// vertical position by `ascent -
    /// descent + line_gap` these are expressed in unscaled coordinates, so
    /// you must multiply by the scale factor for a given size
    pub fn get_v_metrics(&self) -> VMetrics {
        let hhea = &self.data[self.hhea as usize..];
        let [ascent, descent, line_gap] = read_ints!(3, i16, &hhea[4..]);
        VMetrics {
            ascent: i32::from(ascent),
            descent: i32::from(descent),
            line_gap: i32::from(line_gap),
        }
    }

    /// the bounding box around all possible characters
    pub fn get_bounding_box(&self) -> Rect<i16> {
        let head = &self.data[self.head as usize..];
        Rect {
            x0: BE::read_i16(&head[36..]),
            y0: BE::read_i16(&head[38..]),
            x1: BE::read_i16(&head[40..]),
            y1: BE::read_i16(&head[42..]),
        }
    }

    /// computes a scale factor to produce a font whose "height" is 'pixels'
    /// tall. Height is measured as the distance from the highest ascender
    /// to the lowest descender; in other words, it's equivalent to calling
    /// GetFontVMetrics and computing:
    ///       scale = pixels / (ascent - descent)
    /// so if you prefer to measure height by the ascent only, use a similar
    /// calculation.
    pub fn scale_for_pixel_height(&self, height: f32) -> f32 {
        let hhea = &self.data[self.hhea as usize..];
        let fheight = {
            let [a, b] = read_ints!(2, i16, &hhea[4..]);
            f32::from(a) - f32::from(b)
        };
        height / fheight
    }

    /// Returns the units per EM square of this font.
    pub fn units_per_em(&self) -> u16 {
        BE::read_u16(&self.data[self.head as usize + 18..])
    }

    /// computes a scale factor to produce a font whose EM size is mapped to
    /// `pixels` tall. This is probably what traditional APIs compute, but
    /// I'm not positive.
    pub fn scale_for_mapping_em_to_pixels(&self, pixels: f32) -> f32 {
        pixels / (self.units_per_em() as f32)
    }

    /// like `get_codepoint_bitmap_box_subpixel`, but takes a glyph index
    /// instead of a codepoint.
    pub fn get_glyph_bitmap_box_subpixel(
        &self,
        glyph: u32,
        scale_x: f32,
        scale_y: f32,
        shift_x: f32,
        shift_y: f32,
    ) -> Option<Rect<i32>> {
        if let Some(glyph_box) = self.get_glyph_box(glyph) {
            // move to integral bboxes (treating pixels as little squares, what pixels get
            // touched?)
            Some(Rect {
                x0: (glyph_box.x0 as f32 * scale_x + shift_x).floor() as i32,
                y0: (-glyph_box.y1 as f32 * scale_y + shift_y).floor() as i32,
                x1: (glyph_box.x1 as f32 * scale_x + shift_x).ceil() as i32,
                y1: (-glyph_box.y0 as f32 * scale_y + shift_y).ceil() as i32,
            })
        } else {
            // e.g. space character
            None
        }
    }

    /// like `get_codepoint_bitmap_box`, but takes a glyph index instead of a
    /// codepoint.
    pub fn get_glyph_bitmap_box(
        &self,
        glyph: u32,
        scale_x: f32,
        scale_y: f32,
    ) -> Option<Rect<i32>> {
        self.get_glyph_bitmap_box_subpixel(glyph, scale_x, scale_y, 0.0, 0.0)
    }

    /// same as get_codepoint_bitmap_box, but you can specify a subpixel
    /// shift for the character
    pub fn get_codepoint_bitmap_box_subpixel(
        &self,
        codepoint: u32,
        scale_x: f32,
        scale_y: f32,
        shift_x: f32,
        shift_y: f32,
    ) -> Option<Rect<i32>> {
        self.get_glyph_bitmap_box_subpixel(
            self.find_glyph_index(codepoint),
            scale_x,
            scale_y,
            shift_x,
            shift_y,
        )
    }

    /// get the bounding box of the bitmap centered around the glyph origin; so
    /// the bitmap width is x1-x0, height is y1-y0, and location to place
    /// the bitmap top left is (left_side_bearing*scale, y0).
    /// (Note that the bitmap uses y-increases-down, but the shape uses
    /// y-increases-up, so CodepointBitmapBox and CodepointBox are inverted.)
    pub fn get_codepoint_bitmap_box(
        &self,
        codepoint: u32,
        scale_x: f32,
        scale_y: f32,
    ) -> Option<Rect<i32>> {
        self.get_codepoint_bitmap_box_subpixel(codepoint, scale_x, scale_y, 0.0, 0.0)
    }

    pub fn get_font_name_strings(&self) -> FontNameIter<'_, Data> {
        let nm = self.name as usize;
        if nm == 0 {
            return FontNameIter {
                font_info: &self,
                string_offset: 0,
                index: 0,
                count: 0,
            };
        }
        let count = BE::read_u16(&self.data[nm + 2..]) as usize;
        let string_offset = nm + BE::read_u16(&self.data[nm + 4..]) as usize;

        FontNameIter {
            font_info: &self,
            string_offset,
            index: 0,
            count,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct FontNameIter<'a, Data: Deref<Target = [u8]>> {
    /// Font info.
    font_info: &'a FontInfo<Data>,
    string_offset: usize,
    /// Next index.
    index: usize,
    /// Number of name strings.
    count: usize,
}

impl<'a, Data: 'a + Deref<Target = [u8]>> Iterator for FontNameIter<'a, Data> {
    type Item = (&'a [u8], Option<PlatformEncodingLanguageId>, u16);

    fn next(&mut self) -> Option<Self::Item> {
        if self.index >= self.count {
            return None;
        }

        let loc = self.font_info.name as usize + 6 + 12 * self.index;

        let pl_id = platform_id(BE::read_u16(&self.font_info.data[loc..]));
        let platform_encoding_language_id = pl_id.map(|pl_id| {
            let encoding_id = BE::read_u16(&self.font_info.data[loc + 2..]);
            let language_id = BE::read_u16(&self.font_info.data[loc + 4..]);
            platform_encoding_id(pl_id, Some(encoding_id), Some(language_id))
        });
        // @TODO: Define an enum type for Name ID.
        //        See https://www.microsoft.com/typography/otspec/name.htm, "Name IDs" section.
        let name_id = BE::read_u16(&self.font_info.data[loc + 6..]);
        let length = BE::read_u16(&self.font_info.data[loc + 8..]) as usize;
        let offset = self.string_offset + BE::read_u16(&self.font_info.data[loc + 10..]) as usize;

        self.index += 1;

        Some((
            &self.font_info.data[offset..offset + length],
            platform_encoding_language_id,
            name_id,
        ))
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.count - self.index;
        (remaining, Some(remaining))
    }

    fn count(self) -> usize {
        self.count - self.index
    }

    fn last(mut self) -> Option<Self::Item> {
        if self.index >= self.count || self.count == 0 {
            return None;
        }
        self.index = self.count - 1;
        self.next()
    }

    fn nth(&mut self, n: usize) -> Option<Self::Item> {
        if n > self.count - self.index {
            self.index = self.count;
            return None;
        }
        self.index += n;
        self.next()
    }
}
