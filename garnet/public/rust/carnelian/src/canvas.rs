// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use rusttype::{Font, FontCollection, Scale};
use shared_buffer::SharedBuffer;

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
    pub fn new(data: &'a [u8]) -> Result<FontFace<'a>, Error> {
        let collection = FontCollection::from_bytes(data as &[u8])?;
        let font = collection.into_font()?;
        Ok(FontFace { font: font })
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
    fn set_pixel_at_location(&mut self, location: &Point, value: u8, paint: &Paint) {
        let col_stride = BYTES_PER_PIXEL as u32;
        let row_stride = self.stride as u32;
        let row_offset = location.y * row_stride + location.x * col_stride;
        self.set_pixel_at_offset(row_offset as usize, value, paint);
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
        &mut self, text: &str, location: Point, size: Size, font: &mut FontDescription,
        paint: &Paint,
    ) {
        let mut x = location.x;
        let advance = size.width;
        let scale = Scale::uniform(font.size as f32);
        for scalar in text.chars() {
            let cell = Rect {
                left: x,
                top: location.y,
                right: x + advance,
                bottom: location.y + size.height,
            };
            self.fill_rect(&cell, paint.bg);

            if scalar != ' ' {
                let glyph =
                    font.face
                        .font
                        .glyph(scalar)
                        .scaled(scale)
                        .positioned(rusttype::Point {
                            x: x as f32,
                            y: (location.y + font.baseline as u32) as f32,
                        });
                if let Some(bounding_box) = glyph.pixel_bounding_box() {
                    glyph.draw(|pixel_x, pixel_y, v| {
                        let value = (v * 255.0) as u8;
                        let glyph_location = Point {
                            x: pixel_x + bounding_box.min.x as u32,
                            y: pixel_y + bounding_box.min.y as u32,
                        };
                        self.set_pixel_at_location(&glyph_location, value, paint);
                    });
                }
            }
            x += advance;
        }
    }

    /// Draw line of text `text` at location `point` with foreground and background colors specified
    /// by `paint` and with the typographic characterists in `font`.
    pub fn fill_text(
        &mut self, text: &str, location: Point, font: &mut FontDescription, paint: &Paint,
    ) {
        let scale = Scale::uniform(font.size as f32);
        let v_metrics = font.face.font.v_metrics(scale);
        let offset = rusttype::point(location.x as f32, location.y as f32 + v_metrics.ascent);
        let glyphs: Vec<rusttype::PositionedGlyph<'_>> = font.face.font.layout(text, scale, offset).collect();
        for glyph in glyphs {
            if let Some(bounding_box) = glyph.pixel_bounding_box() {
                glyph.draw(|pixel_x, pixel_y, v| {
                    let value = (v * 255.0) as u8;
                    let glyph_location = Point {
                        x: pixel_x + bounding_box.min.x as u32,
                        y: pixel_y + bounding_box.min.y as u32,
                    };
                    self.set_pixel_at_location(&glyph_location, value, paint);
                })
            }
        }
    }

    /// Measure a line of text `text` and with the typographic characterists in `font`.
    /// Returns a tuple containing the measured width and height.
    pub fn measure_text(&mut self, text: &str, font: &mut FontDescription) -> (i32, i32) {
        let scale = Scale::uniform(font.size as f32);
        let v_metrics = font.face.font.v_metrics(scale);
        let offset = rusttype::point(0.0, v_metrics.ascent);
        let glyphs: Vec<rusttype::PositionedGlyph<'_>> = font.face.font.layout(text, scale, offset).collect();
        let width = glyphs
            .iter()
            .rev()
            .map(|g| g.position().x as f32 + g.unpositioned().h_metrics().advance_width)
            .next()
            .unwrap_or(0.0)
            .ceil() as usize;

        (width as i32, font.size as i32)
    }
}
