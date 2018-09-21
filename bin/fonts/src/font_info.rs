// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::freetype_ffi as freetype;
use failure::{format_err, Error};
use fuchsia_zircon as zx;
use std::cmp::Ordering;

type BitmapElement = u64;
const BITMAP_ELEMENT_SIZE: usize = 64;

const MAX_RANGE_GAP: u32 = 2048;

struct CharSetRange {
    start: u32,
    bitmap: Vec<BitmapElement>,
}

impl CharSetRange {
    fn new() -> CharSetRange {
        CharSetRange {
            start: 0,
            bitmap: vec![],
        }
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

pub struct CharSet {
    ranges: Vec<CharSetRange>,
}

impl CharSet {
    fn new(mut codepoints: Vec<u32>) -> CharSet {
        codepoints.sort_unstable();

        let mut ranges = vec![];
        let mut range = CharSetRange::new();
        for c in codepoints {
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

pub struct FontInfo {
    pub charset: CharSet,
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

    pub fn load_font_info(&self, data: &[u8], index: u32) -> Result<FontInfo, Error> {
        let mut codepoints: Vec<u32> = Vec::new();

        // Unsafe to call freetype FFI. Call FT_New_Memory_Face() to load a
        // typeface. If it succeeds then enumerate character map with
        // FT_Get_First_Char() and FT_Get_Next_Char(). FT_Done_Face() must be
        // called if the typeface was initialized successfully.
        unsafe {
            let mut ft_face = std::ptr::null_mut();
            let err = freetype::FT_New_Memory_Face(
                self.ft_library,
                data.as_ptr(),
                data.len() as libc::c_long,
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

        Ok(FontInfo {
            charset: CharSet::new(codepoints),
        })
    }

    // Unsafe to map vmo because vmo size may change, while it's mapped.
    // Potentially this function can be implemented as safe: it would need to
    // use FT_OpenFace with a FT_Stream implementation that calls zx_vmo_read().
    pub unsafe fn load_font_info_from_vmo(
        &self, vmo: &zx::Vmo, vmo_size: usize, index: u32,
    ) -> Result<FontInfo, Error> {
        let mapped = zx::Vmar::root_self().map(0, &vmo, 0, vmo_size, zx::VmarFlags::PERM_READ)?;
        let mapped_ptr = mapped as *const u8;
        let result = self.load_font_info(std::slice::from_raw_parts(mapped_ptr, vmo_size), index);
        zx::Vmar::root_self().unmap(mapped, vmo_size).unwrap();

        result
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
