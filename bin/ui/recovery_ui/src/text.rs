// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use font_rs::font::{parse, Font, FontError, GlyphBitmap};
use fuchsia_framebuffer::Frame;
use std::cmp::max;
use std::collections::HashMap;

#[derive(Hash, Eq, PartialEq, Debug)]
struct GlyphDescriptor {
    size: u32,
    glyph_id: u16,
}

pub struct Face<'a> {
    font: Font<'a>,
    glyphs: HashMap<GlyphDescriptor, GlyphBitmap>,
}

impl<'a> Face<'a> {
    pub fn new(data: &'a [u8]) -> Result<Face<'a>, FontError> {
        Ok(Face {
            font: parse(data)?,
            glyphs: HashMap::new(),
        })
    }

    pub fn get_glyph(&mut self, glyph_id: u16, size: u32) -> &GlyphBitmap {
        let font = &self.font;
        self.glyphs
            .entry(GlyphDescriptor { size, glyph_id })
            .or_insert_with(|| font.render_glyph(glyph_id, size).unwrap())
    }

    fn draw_glyph_at(frame: &mut Frame, glyph: &GlyphBitmap, left_x: i32, top_y: i32) {
        let glyph_data = &glyph.data.as_slice();
        let mut y = top_y;
        let pixel_size_bytes = frame.pixel_size_bytes();
        for glyph_row in glyph_data.chunks(glyph.width) {
            if y > 0 {
                let mut x = left_x;
                for one_pixel in glyph_row {
                    let one_pixel = *one_pixel;
                    if one_pixel > 0 {
                        if pixel_size_bytes == 4 {
                            frame.write_pixel(
                                x as u32,
                                y as u32,
                                &[one_pixel, one_pixel, one_pixel, one_pixel],
                            );
                        } else {
                            let b1 = (one_pixel << 3) | ((one_pixel & 0b11_1000) >> 3);
                            let b2 = ((one_pixel & 0b111) << 5) | one_pixel;
                            frame.write_pixel(x as u32, y as u32, &[b2, b1]);
                        }
                    }
                    x += 1;
                }
            }
            y += 1;
        }
    }

    fn draw_text_at_internal(
        &mut self, mut frame: Option<&mut Frame>, left_x: i32, top_y: i32, size: u32, text: &str,
    ) -> (i32, i32) {
        let mut max_top = 0;
        let mut x = left_x;
        let padding : i32 = max(size as i32 / 16, 2);
        for one_char in text.chars() {
            if one_char != ' ' {
                if let Some(glyph_id) = self.font.lookup_glyph_id(one_char as u32) {
                    let glyph = self.get_glyph(glyph_id, size as u32);
                    let glyph_x = x + glyph.left;
                    let glyph_y = top_y + glyph.top;
                    max_top = max(max_top, -glyph.top);
                    if let Some(ref mut frame) = frame {
                        Self::draw_glyph_at(*frame, &glyph, glyph_x, glyph_y);
                    }
                    x += glyph.width as i32 + padding;
                }
            } else {
                x += (size / 3) as i32 + padding;
            }
        }
        (x - left_x, max_top)
    }

    pub fn draw_text_at(&mut self, frame: &mut Frame, x: i32, y: i32, size: u32, text: &str) {
        self.draw_text_at_internal(Some(frame), x, y, size, text);
    }

    pub fn measure_text(&mut self, size: u32, text: &str) -> (i32, i32) {
        self.draw_text_at_internal(None, 0, 0, size, text)
    }
}
