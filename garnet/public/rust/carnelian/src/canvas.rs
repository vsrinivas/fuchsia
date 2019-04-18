// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::geometry::{Coord, Point, Rect, Size};
use euclid::rect;
use failure::Error;
use fidl_fuchsia_ui_gfx::ColorRgba;
use mapped_vmo::Mapping;
use rusttype::{Font, FontCollection, Scale};
use std::sync::Arc;

/// Struct representing an RGBA color value
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

#[allow(missing_docs)]
impl Color {
    pub fn new() -> Color {
        Color { r: 0, g: 0, b: 0, a: 255 }
    }

    pub fn white() -> Color {
        Color { r: 255, g: 255, b: 255, a: 255 }
    }

    pub fn extract_hex_slice(hash_code: &str, start_index: usize) -> Result<u8, Error> {
        Ok(u8::from_str_radix(&hash_code[start_index..start_index + 2], 16)?)
    }

    pub fn to_float_color_component(component: u8) -> f64 {
        (component as f64 * 100.0 / 255.0).round() / 100.0
    }

    pub fn from_hash_code(hash_code: &str) -> Result<Color, Error> {
        let mut new_color = Color::new();
        new_color.r = Color::extract_hex_slice(&hash_code, 1)?;
        new_color.g = Color::extract_hex_slice(&hash_code, 3)?;
        new_color.b = Color::extract_hex_slice(&hash_code, 5)?;
        Ok(new_color)
    }

    pub fn make_color_rgba(&self) -> ColorRgba {
        ColorRgba { red: self.r, green: self.g, blue: self.b, alpha: self.a }
    }
}

/// Struct combining a foreground and background color.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
#[allow(missing_docs)]
pub struct Paint {
    pub fg: Color,
    pub bg: Color,
}

impl Paint {
    #[allow(missing_docs)]
    pub fn from_hash_codes(fg: &str, bg: &str) -> Result<Paint, Error> {
        Ok(Paint { fg: Color::from_hash_code(fg)?, bg: Color::from_hash_code(bg)? })
    }
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

/// Struct containing a font and a cache of rendered glyphs.
pub struct FontFace<'a> {
    font: Font<'a>,
}

/// Struct containing font, size and baseline.
#[allow(missing_docs)]
pub struct FontDescription<'a, 'b: 'a> {
    pub face: &'a FontFace<'b>,
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

/// Trait abstracting a target to which pixels can be written.
pub trait PixelSink {
    /// Write an RGBA pixel at a byte offset within the sink.
    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]);
}

/// Pixel sink targeting a shared buffer.
pub struct MappingPixelSink {
    mapping: Arc<Mapping>,
}

impl PixelSink for MappingPixelSink {
    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]) {
        self.mapping.write_at(offset, &value);
    }
}

/// Canvas is used to do simple graphics and text rendering into a
/// SharedBuffer that can then be displayed using Scenic or
/// Display Manager.
pub struct Canvas<T: PixelSink> {
    // Assumes a pixel format of BGRA8 and a color space of sRGB.
    pixel_sink: T,
    row_stride: u32,
    col_stride: u32,
}

impl<T: PixelSink> Canvas<T> {
    /// Create a canvas targeting a shared buffer with stride.
    pub fn new(
        mapping: Arc<Mapping>,
        row_stride: u32,
        col_stride: u32,
    ) -> Canvas<MappingPixelSink> {
        let pixel_sink = MappingPixelSink { mapping };
        Canvas { pixel_sink, row_stride, col_stride }
    }

    /// Create a canvas targeting a particular pixel sink and
    /// with a specific row stride in bytes.
    pub fn new_with_sink(pixel_sink: T, row_stride: u32, col_stride: u32) -> Canvas<T> {
        Canvas { pixel_sink, row_stride, col_stride }
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
        let location = location.floor().to_i32();
        if location.x >= 0 && location.y >= 0 {
            let location = location.to_u32();
            let row_offset = location.y * self.row_stride + location.x * self.col_stride;
            self.set_pixel_at_offset(row_offset as usize, value, paint);
        }
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
        let rect = rect.round_out().to_i32();
        let col_stride = self.col_stride;
        let row_stride = self.row_stride;
        for y in rect.min_y().max(0)..rect.max_y().max(0) {
            for x in rect.min_x()..rect.max_x() {
                let offset = y as u32 * row_stride + x as u32 * col_stride;
                self.write_color_at_offset(offset as usize, color);
            }
        }
    }

    /// Draw line of text `text` at location `point` with foreground and background colors specified
    /// by `paint` and with the typographic characteristics in `font`. This method uses
    /// fixed size cells of size `size` for each character.
    pub fn fill_text_cells(
        &mut self,
        text: &str,
        location: Point,
        size: Size,
        font: &mut FontDescription,
        paint: &Paint,
    ) {
        let mut x = location.x;
        let advance = size.width;
        let scale = Scale::uniform(font.size as f32);
        for scalar in text.chars() {
            let cell = rect(x, location.y, advance, size.height);
            self.fill_rect(&cell, paint.bg);

            if scalar != ' ' {
                let glyph =
                    font.face.font.glyph(scalar).scaled(scale).positioned(rusttype::Point {
                        x: x,
                        y: (location.y + font.baseline as f32),
                    });
                if let Some(bounding_box) = glyph.pixel_bounding_box() {
                    glyph.draw(|pixel_x, pixel_y, v| {
                        let value = (v * 255.0) as u8;
                        let glyph_location = Point::new(
                            (pixel_x as i32 + bounding_box.min.x) as Coord,
                            (pixel_y as i32 + bounding_box.min.y) as Coord,
                        );
                        self.set_pixel_at_location(&glyph_location, value, paint);
                    });
                }
            }
            x += advance;
        }
    }

    /// Draw line of text `text` at location `point` with foreground and background colors specified
    /// by `paint` and with the typographic characteristics in `font`.
    pub fn fill_text(
        &mut self,
        text: &str,
        location: Point,
        font: &mut FontDescription,
        paint: &Paint,
    ) {
        let scale = Scale::uniform(font.size as f32);
        let v_metrics = font.face.font.v_metrics(scale);
        let offset = rusttype::point(location.x as f32, location.y as f32 + v_metrics.ascent);
        let glyphs: Vec<rusttype::PositionedGlyph<'_>> =
            font.face.font.layout(text, scale, offset).collect();
        for glyph in glyphs {
            if let Some(bounding_box) = glyph.pixel_bounding_box() {
                glyph.draw(|pixel_x, pixel_y, v| {
                    let value = (v * 255.0) as u8;
                    let glyph_location = Point::new(
                        (pixel_x as i32 + bounding_box.min.x) as Coord,
                        (pixel_y as i32 + bounding_box.min.y) as Coord,
                    );
                    self.set_pixel_at_location(&glyph_location, value, paint);
                })
            }
        }
    }
}

/// Measure a line of text `text` and with the typographic characteristics in `font`.
/// Returns a tuple containing the measured width and height.
pub fn measure_text(text: &str, font: &mut FontDescription) -> Size {
    let scale = Scale::uniform(font.size as f32);
    let v_metrics = font.face.font.v_metrics(scale);
    let offset = rusttype::point(0.0, v_metrics.ascent);
    let g_opt = font.face.font.layout(text, scale, offset).last();
    let width = g_opt
        .map(|g| g.position().x as f32 + g.unpositioned().h_metrics().advance_width)
        .unwrap_or(0.0)
        .ceil();

    Size::new(width, font.size as Coord)
}
