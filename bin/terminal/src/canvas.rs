// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use font_rs::font::{parse, Font, FontError, GlyphBitmap};
use shared_buffer::SharedBuffer;
use std::collections::HashMap;

#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Point {
    pub x: u32,
    pub y: u32,
}

#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Size {
    pub width: u32,
    pub height: u32,
}

#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Rect {
    pub left: u32,
    pub top: u32,
    pub right: u32,
    pub bottom: u32,
}

#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Paint {
    pub fg: Color,
    pub bg: Color,
}

#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
struct Glyph(u16);

#[derive(Hash, Eq, PartialEq, Debug)]
struct GlyphDescriptor {
    size: u32,
    glyph: Glyph,
}

pub struct FontFace<'a> {
    font: Font<'a>,
    glyph_cache: HashMap<GlyphDescriptor, GlyphBitmap>,
}

pub struct FontDescription<'a, 'b: 'a> {
    pub face: &'a mut FontFace<'b>,
    pub size: u32,
    pub baseline: i32,
}

impl<'a> FontFace<'a> {
    pub fn new(data: &'a [u8]) -> Result<FontFace<'a>, FontError> {
        Ok(FontFace {
            font: parse(data)?,
            glyph_cache: HashMap::new(),
        })
    }

    fn get_glyph(&mut self, glyph: Glyph, size: u32) -> &GlyphBitmap {
        let font = &self.font;
        let Glyph(glyph_id) = glyph;
        self.glyph_cache
            .entry(GlyphDescriptor { size, glyph })
            .or_insert_with(|| font.render_glyph(glyph_id, size).unwrap())
    }

    fn lookup_glyph(&self, scalar: char) -> Option<Glyph> {
        self.font
            .lookup_glyph_id(scalar as u32)
            .map(|id| Glyph(id))
    }
}

pub struct Canvas {
    // Assumes a pixel format of BGRA8 and a color space of sRGB.
    buffer: SharedBuffer,
    stride: u32,
}

const BYTES_PER_PIXEL: u32 = 4;

impl Canvas {
    pub fn new(buffer: SharedBuffer, stride: u32) -> Canvas {
        Canvas { buffer, stride }
    }

    #[inline]
    fn write_color_at_offset(&mut self, offset: usize, color: Color) {
        let pixel = [color.b, color.g, color.r, color.a];
        self.buffer.write_at(offset, &pixel);
    }

    #[inline]
    fn set_pixel_at_offset(&mut self, offset: usize, value: u8, paint: &Paint) {
        match value {
            0 => (),
            255 => self.write_color_at_offset(offset, paint.fg),
            _ => {
                let fg = &paint.fg;
                let bg = &paint.bg;
                let a = ((value as u32) * (fg.a as u32)) >> 8;
                let blend_factor = ((bg.a as u32) * (255 - a)) >> 8;
                let pixel = [
                    (((bg.b as u32) * blend_factor + (fg.b as u32) * a) >> 8) as u8,
                    (((bg.g as u32) * blend_factor + (fg.g as u32) * a) >> 8) as u8,
                    (((bg.r as u32) * blend_factor + (fg.r as u32) * a) >> 8) as u8,
                    (blend_factor + (fg.a as u32)) as u8,
                ];
                self.buffer.write_at(offset, &pixel);
            }
        }
    }

    fn draw_glyph_at(&mut self, glyph: &GlyphBitmap, x: i32, y: i32, paint: &Paint) {
        let glyph_data = &glyph.data.as_slice();
        let col_stride = BYTES_PER_PIXEL as i32;
        let row_stride = self.stride as i32;
        let mut row_offset = y * row_stride + x * col_stride;
        for glyph_row in glyph_data.chunks(glyph.width) {
            let mut offset = row_offset;
            if offset > 0 {
                for pixel in glyph_row {
                    let value = *pixel;
                    self.set_pixel_at_offset(offset as usize, value, paint);
                    offset += col_stride;
                }
            }
            row_offset += row_stride;
        }
    }

    pub fn fill_rect(&mut self, rect: &Rect, color: Color) {
        let col_stride = BYTES_PER_PIXEL;
        let row_stride = self.stride;
        for y in rect.top..rect.bottom {
            for x in rect.left..rect.right {
                let offset = y * row_stride + x * col_stride;
                self.write_color_at_offset(offset as usize, color);
            }
        }
    }

    pub fn fill_text(
        &mut self, text: &str, point: Point, size: Size, font: &mut FontDescription, paint: &Paint,
    ) {
        let mut x = point.x;
        let advance = size.width;
        for scalar in text.chars() {
            let cell = Rect {
                left: x,
                top: point.y,
                right: x + advance,
                bottom: point.y + size.height,
            };
            self.fill_rect(&cell, paint.bg);
            if scalar != ' ' {
                if let Some(glyph_id) = font.face.lookup_glyph(scalar) {
                    let glyph = font.face.get_glyph(glyph_id, font.size);
                    let x = cell.left as i32 + glyph.left;
                    let y = cell.top as i32 + font.baseline + glyph.top;
                    self.draw_glyph_at(glyph, x, y, paint);
                }
            }
            x += advance;
        }
    }
}
