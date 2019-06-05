// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::freetype_ffi as freetype;
use failure::{format_err, Error};
use fuchsia_zircon as zx;
use libc::{c_uchar, c_ulong, c_void};
use std::cmp::Ordering;

type BitmapElement = u64;
const BITMAP_ELEMENT_SIZE: usize = 64;

const MAX_RANGE_GAP: u32 = 2048;

/// Represents an ordered set of code points that begin at [CharSetRange.start]. The largest
/// allowed discontinuity between two consecutive code points in the set is [MAX_RANGE_GAP].
#[derive(Debug)]
struct CharSetRange {
    start: u32,
    bitmap: Vec<BitmapElement>,
}

impl CharSetRange {
    fn new() -> CharSetRange {
        CharSetRange { start: 0, bitmap: vec![] }
    }

    fn start(&self) -> u32 {
        self.start
    }

    fn end(&self) -> u32 {
        self.start + (self.bitmap.len() * BITMAP_ELEMENT_SIZE) as u32
    }

    fn is_empty(&self) -> bool {
        self.bitmap.is_empty()
    }

    fn add(&mut self, val: u32) {
        assert!(val >= self.start);

        if self.bitmap.is_empty() {
            self.start = val;
        }

        let pos = (val - self.start) as usize;
        let element_pos = pos / BITMAP_ELEMENT_SIZE;

        if element_pos >= self.bitmap.len() {
            self.bitmap.resize(element_pos + 1, 0);
        }

        self.bitmap[element_pos] |= 1 << (pos % BITMAP_ELEMENT_SIZE);
    }

    fn contains(&self, c: u32) -> bool {
        if c < self.start || c >= self.end() {
            false
        } else {
            let index = c as usize - self.start as usize;
            (self.bitmap[index / 64] & (1 << (index % 64))) > 0
        }
    }
}

/// Represents an ordered set of code points.
///
/// TODO(kpozin): Evaluate replacing with `MultiCharRange`, which might be more space-efficient for
/// large sets with few discontinuities.
#[derive(Debug)]
pub struct CharSet {
    ranges: Vec<CharSetRange>,
}

impl CharSet {
    pub fn new(mut code_points: Vec<u32>) -> CharSet {
        code_points.sort_unstable();

        let mut ranges = vec![];
        let mut range = CharSetRange::new();
        for c in code_points {
            if c != 0 && !range.is_empty() && c >= range.end() + MAX_RANGE_GAP {
                ranges.push(range);
                range = CharSetRange::new();
            }
            range.add(c);
        }
        if !range.is_empty() {
            ranges.push(range)
        }
        CharSet { ranges }
    }

    pub fn contains(&self, c: u32) -> bool {
        match self.ranges.binary_search_by(|r| {
            if r.end() < c {
                Ordering::Less
            } else if r.start() > c {
                Ordering::Greater
            } else {
                Ordering::Equal
            }
        }) {
            Ok(r) => self.ranges[r].contains(c),
            Err(_) => false,
        }
    }
}

struct VmoStreamInternal {
    vmo: zx::Vmo,
    stream_rec: freetype::FT_StreamRec,
}

impl VmoStreamInternal {
    // Caller must ensure that the returned FT_Stream is not used after
    // VmoStream is dropped.
    unsafe fn ft_stream(&self) -> freetype::FT_Stream {
        &self.stream_rec as freetype::FT_Stream
    }

    fn read(&mut self, offset: u64, read_buffer: &mut [u8]) -> u64 {
        if read_buffer.len() == 0 || offset >= self.stream_rec.size as u64 {
            return 0;
        }
        let read_size = std::cmp::min(read_buffer.len(), (self.stream_rec.size - offset) as usize);
        match self.vmo.read(&mut read_buffer[..read_size], offset) {
            Ok(_) => read_size as u64,
            Err(err) => {
                println!("Error when reading from font VMO: {:?}", err);
                0
            }
        }
    }

    // Unsafe callback called by freetype to read from the stream.
    unsafe extern "C" fn read_func(
        stream: freetype::FT_Stream,
        offset: c_ulong,
        buffer: *mut c_uchar,
        count: c_ulong,
    ) -> c_ulong {
        let wrapper = &mut *((*stream).descriptor as *mut VmoStreamInternal);
        let buffer_slice = std::slice::from_raw_parts_mut(buffer as *mut u8, count as usize);
        wrapper.read(offset as u64, buffer_slice) as c_ulong
    }

    extern "C" fn close_func(_stream: freetype::FT_Stream) {
        // No-op. Stream will be closed when the VmoStream is dropped.
    }
}

// Implements FT_Stream for a VMO.
struct VmoStream {
    // VmoStreamInternal needs to be boxed to ensure that it's not moved. This
    // allows to set stream_rec.descriptor to point to the containing
    // VmoStreamInternal instance.
    internal: Box<VmoStreamInternal>,
}

impl VmoStream {
    fn new(vmo: zx::Vmo, vmo_size: usize) -> Result<VmoStream, Error> {
        let mut internal = Box::new(VmoStreamInternal {
            vmo,
            stream_rec: freetype::FT_StreamRec {
                base: std::ptr::null(),
                size: vmo_size as c_ulong,
                pos: 0,

                descriptor: std::ptr::null_mut(),
                pathname: std::ptr::null_mut(),
                read: VmoStreamInternal::read_func,
                close: VmoStreamInternal::close_func,

                memory: std::ptr::null_mut(),
                cursor: std::ptr::null_mut(),
                limit: std::ptr::null_mut(),
            },
        });

        internal.stream_rec.descriptor = &mut *internal as *mut VmoStreamInternal as *mut c_void;

        Ok(VmoStream { internal })
    }

    // Caller must ensure that the returned FT_Stream is not used after
    // VmoStream is dropped.
    unsafe fn ft_stream(&self) -> freetype::FT_Stream {
        self.internal.ft_stream()
    }
}

pub struct FontInfo {
    pub char_set: CharSet,
}

pub struct FontInfoLoader {
    ft_library: freetype::FT_Library,
}

impl std::ops::Drop for FontInfoLoader {
    fn drop(&mut self) {
        unsafe {
            freetype::FT_Done_Library(self.ft_library);
        }
    }
}

impl FontInfoLoader {
    pub fn new() -> Result<FontInfoLoader, Error> {
        // Unsafe to call freetype FFI. On success stores the FT_Library pointer
        // in FontInfoLoader, which calls FT_Done_Library in drop(). Must ensure
        // that all allocated memory is freed on failure.
        unsafe {
            let mut ft_library = std::ptr::null_mut();
            if freetype::FT_New_Library(&freetype::FT_MEMORY, &mut ft_library) != 0 {
                return Err(format_err!("Failed to initialize FreeType library."));
            }
            freetype::FT_Add_Default_Modules(ft_library);
            Ok(FontInfoLoader { ft_library })
        }
    }

    pub fn load_font_info(
        &self,
        vmo: zx::Vmo,
        vmo_size: usize,
        index: u32,
    ) -> Result<FontInfo, Error> {
        let mut codepoints: Vec<u32> = Vec::new();

        // Unsafe to call freetype FFI. Call FT_Open_Face() to load a typeface.
        // If it succeeds then enumerate character map with FT_Get_First_Char()
        // and FT_Get_Next_Char(). FT_Done_Face() must be called if the typeface
        // was initialized successfully.
        unsafe {
            let stream = VmoStream::new(vmo, vmo_size)?;
            let open_args = freetype::FT_Open_Args {
                flags: freetype::FT_OPEN_STREAM,
                memory_base: std::ptr::null(),
                memory_size: 0,
                pathname: std::ptr::null(),
                stream: stream.ft_stream(),
                driver: std::ptr::null_mut(),
                num_params: 0,
                params: std::ptr::null_mut(),
            };

            let mut ft_face = std::ptr::null_mut();
            let err = freetype::FT_Open_Face(
                self.ft_library,
                &open_args,
                index as libc::c_long,
                &mut ft_face,
            );

            if err != 0 {
                return Err(format_err!("Failed to parse font file: error={}", err));
            }

            let mut glyph_index = 0;
            let mut codepoint = freetype::FT_Get_First_Char(ft_face, &mut glyph_index);
            while glyph_index > 0 {
                codepoints.push(codepoint as u32);
                codepoint = freetype::FT_Get_Next_Char(ft_face, codepoint, &mut glyph_index);
            }

            freetype::FT_Done_Face(ft_face);
        }

        Ok(FontInfo { char_set: CharSet::new(codepoints) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_charset() {
        let charset = CharSet::new(vec![1, 2, 3, 10, 500, 5000, 5001, 10000]);
        assert!(!charset.contains(0));
        assert!(charset.contains(1));
        assert!(charset.contains(2));
        assert!(charset.contains(3));
        assert!(!charset.contains(4));
        assert!(!charset.contains(9));
        assert!(charset.contains(10));
        assert!(charset.contains(500));
        assert!(!charset.contains(501));
        assert!(charset.contains(5000));
        assert!(charset.contains(5001));
        assert!(!charset.contains(5002));
        assert!(!charset.contains(9999));
        assert!(charset.contains(10000));
        assert!(!charset.contains(10001));
    }
}
