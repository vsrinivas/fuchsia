// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use font_rs::font::{parse, Font, FontError, GlyphBitmap};
use shared_buffer::SharedBuffer;
use std::{cmp::max, collections::HashMap};

/// Struct representing an RGBA color value
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

/// Struct representing an location
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Point {
    pub x: u32,
    pub y: u32,
}

/// Struct representing an size
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Size {
    pub width: u32,
    pub height: u32,
}

/// Struct representing a rectangle area.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Rect {
    pub left: u32,
    pub top: u32,
    pub right: u32,
    pub bottom: u32,
}

/// Struct combining a foreground and background color.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Paint {
    pub fg: Color,
    pub bg: Color,
}

/// Opaque type representing a glyph ID.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
struct Glyph(u16);

/// Struct representing a glyph at a specific size.
#[derive(Hash, Eq, PartialEq, Debug)]
struct GlyphDescriptor {
    size: u32,
    glyph: Glyph,
}

/// Struct containing a font and a cache of renderered glyphs.
pub struct FontFace<'a> {
    font: Font<'a>,
    glyph_cache: HashMap<GlyphDescriptor, GlyphBitmap>,
}

/// Struct containint font, size and baseline.
#[allow(missing_docs)]
pub struct FontDescription<'a, 'b: 'a> {
    pub face: &'a mut FontFace<'b>,
    pub size: u32,
    pub baseline: i32,
}

#[allow(missing_docs)]
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
        self.font.lookup_glyph_id(scalar as u32).map(|id| Glyph(id))
    }
}

const BYTES_PER_PIXEL: u32 = 4;

/// Trait abstracting a target to which pixels can be written.
pub trait PixelSink {
    /// Write an RGBA pixel at an x,y location in the sink.
    fn write_pixel_at_location(&mut self, x: u32, y: u32, value: &[u8]);
    /// Write an RGBA pixel at a byte offset within the sink.
    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]);
}

/// Pixel sink targetting a shared buffer.
pub struct SharedBufferPixelSink {
    buffer: SharedBuffer,
    stride: u32,
}

impl PixelSink for SharedBufferPixelSink {
    fn write_pixel_at_location(&mut self, x: u32, y: u32, value: &[u8]) {
        let offset = (y * self.stride + x * BYTES_PER_PIXEL) as usize;
        self.write_pixel_at_offset(offset, &value);
    }

    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]) {
        self.buffer.write_at(offset, &value);
    }
}

/// Canvas is used to do simple graphics and text rendering into a
/// SharedBuffer that can then be displayed using Scenic or
/// Display Manager.
pub struct Canvas<T: PixelSink> {
    // Assumes a pixel format of BGRA8 and a color space of sRGB.
    pixel_sink: T,
    stride: u32,
}

impl<T: PixelSink> Canvas<T> {
    /// Create a canvas targetting a shared buffer with stride.
    pub fn new(buffer: SharedBuffer, stride: u32) -> Canvas<SharedBufferPixelSink> {
        let sink = SharedBufferPixelSink { buffer, stride };
        Canvas {
            pixel_sink: sink,
            stride,
        }
    }

    /// Create a canvas targetting a particular pixel sink and
    /// with a specific row stride in bytes.
    pub fn new_with_sink(pixel_sink: T, stride: u32) -> Canvas<T> {
        Canvas { pixel_sink, stride }
    }

    #[inline]
    /// Update the pixel at a particular byte offset with a particular
    /// color.
    fn write_color_at_offset(&mut self, offset: usize, color: Color) {
        let pixel = [color.b, color.g, color.r, color.a];
        self.pixel_sink.write_pixel_at_offset(offset, &pixel);
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
                self.pixel_sink.write_pixel_at_offset(offset, &pixel);
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

    /// Fill a rectangle with a particular color.
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

    /// Draw line of text `text` at location `point` with foreground and background colors specified
    /// by `paint` and with the typographic characterists in `font`. This method uses
    /// fixed size cells of size `size` for each character.
    pub fn fill_text_cells(
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

    /// Draw line of text `text` at location `point` with foreground and background colors specified
    /// by `paint` and with the typographic characterists in `font`.
    pub fn fill_text(
        &mut self, text: &str, point: Point, font: &mut FontDescription, paint: &Paint,
    ) {
        let mut x = point.x;
        let padding: u32 = max(font.size / 16, 2);
        for scalar in text.chars() {
            if scalar != ' ' {
                if let Some(glyph_id) = font.face.lookup_glyph(scalar) {
                    let glyph = font.face.get_glyph(glyph_id, font.size);
                    let glyph_x = x as i32 + glyph.left;
                    let y = point.y as i32 + font.baseline + glyph.top;
                    let cell = Rect {
                        left: x,
                        top: point.y,
                        right: x + glyph.width as u32,
                        bottom: point.y + glyph.height as u32,
                    };
                    self.fill_rect(&cell, paint.bg);
                    self.draw_glyph_at(glyph, glyph_x, y, paint);
                    x += glyph.width as u32 + padding;
                }
            } else {
                let space_width = (font.size / 3) + padding;
                let cell = Rect {
                    left: x,
                    top: point.y,
                    right: x + space_width,
                    bottom: point.y + font.size,
                };
                self.fill_rect(&cell, paint.bg);
                x += space_width;
            }
        }
    }

    /// Measure a line of text `text` and with the typographic characterists in `font`.
    /// Returns a tuple containing the measured width and height.
    pub fn measure_text(&mut self, text: &str, font: &mut FontDescription) -> (i32, i32) {
        let mut max_top = 0;
        let mut x = 0;
        const EMPIRICALLY_CHOSEN_PADDING_AMOUNT_DIVISOR: i32 = 16;
        const EMPIRICALLY_CHOSEN_MINIMUM_PADDING: i32 = 2;
        let padding: i32 = max(
            font.size as i32 / EMPIRICALLY_CHOSEN_PADDING_AMOUNT_DIVISOR,
            EMPIRICALLY_CHOSEN_MINIMUM_PADDING,
        );
        for one_char in text.chars() {
            if one_char != ' ' {
                if let Some(glyph_id) = font.face.lookup_glyph(one_char) {
                    let glyph = font.face.get_glyph(glyph_id, font.size as u32);
                    max_top = max(max_top, -glyph.top);
                    x += glyph.width as i32 + padding;
                }
            } else {
                const EMPIRICALLY_CHOSEN_SPACE_CHARACTER_WIDTH_DIVIDER: u32 = 3;
                x +=
                    (font.size / EMPIRICALLY_CHOSEN_SPACE_CHARACTER_WIDTH_DIVIDER) as i32 + padding;
            }
        }
        (x, max_top)
    }
}
