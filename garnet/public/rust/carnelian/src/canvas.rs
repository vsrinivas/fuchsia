// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::geometry::{Coord, IntPoint, IntRect, IntSize, Point, Rect, Size};
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

pub fn to_565(pixel: &[u8; 4]) -> [u8; 2] {
    let red = pixel[0] >> 3;
    let green = pixel[1] >> 2;
    let blue = pixel[2] >> 3;
    let b1 = (red << 3) | ((green & 0b11_1000) >> 3);
    let b2 = ((green & 0b111) << 5) | blue;
    [b2, b1]
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

    pub fn to_565(&self) -> [u8; 2] {
        let pixel = [self.r, self.g, self.b, self.a];
        to_565(&pixel)
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
    bounds: IntRect,
}

impl<T: PixelSink> Canvas<T> {
    /// Create a canvas targeting a shared buffer with stride.
    pub fn new(
        size: IntSize,
        mapping: Arc<Mapping>,
        row_stride: u32,
        col_stride: u32,
    ) -> Canvas<MappingPixelSink> {
        let pixel_sink = MappingPixelSink { mapping };
        Canvas { pixel_sink, row_stride, col_stride, bounds: IntRect::new(IntPoint::zero(), size) }
    }

    /// Create a canvas targeting a particular pixel sink and
    /// with a specific row stride in bytes.
    pub fn new_with_sink(
        size: IntSize,
        pixel_sink: T,
        row_stride: u32,
        col_stride: u32,
    ) -> Canvas<T> {
        Canvas { pixel_sink, row_stride, col_stride, bounds: IntRect::new(IntPoint::zero(), size) }
    }

    #[inline]
    /// Update the pixel at a particular byte offset with a particular
    /// color.
    fn write_color_at_offset(&mut self, offset: usize, color: Color) {
        if self.col_stride == 2 {
            let pixel = color.to_565();
            self.pixel_sink.write_pixel_at_offset(offset, &pixel);
        } else {
            let pixel = [color.b, color.g, color.r, color.a];

            self.pixel_sink.write_pixel_at_offset(offset, &pixel);
        };
    }

    #[inline]
    fn offset_from_x_y(&self, x: i32, y: i32) -> usize {
        (y * self.row_stride as i32 + x * self.col_stride as i32) as usize
    }

    #[inline]
    fn set_pixel_at_location(&mut self, location: &Point, value: u8, paint: &Paint) {
        let location = location.floor().to_i32();
        if self.bounds.contains(&location) {
            let offset = self.offset_from_x_y(location.x, location.y);
            self.set_pixel_at_offset(offset, value, paint);
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
                if self.col_stride == 2 {
                    let pixel = to_565(&pixel);
                    self.pixel_sink.write_pixel_at_offset(offset, &pixel);
                } else {
                    self.pixel_sink.write_pixel_at_offset(offset, &pixel);
                }
            }
        }
    }

    /// Fill a rectangle with a particular color.
    pub fn fill_rect(&mut self, rect: &Rect, color: Color) {
        if rect.is_empty() {
            return;
        }
        let rect = rect.round_out().to_i32();
        if let Some(clipped_rect) = self.bounds.intersection(&rect) {
            for y in clipped_rect.min_y()..clipped_rect.max_y() {
                for x in clipped_rect.min_x()..clipped_rect.max_x() {
                    let offset = self.offset_from_x_y(x, y);
                    self.write_color_at_offset(offset as usize, color);
                }
            }
        }
    }

    /// Fill a circle with a particular color.
    pub fn fill_circle(&mut self, center: &Point, radius: Coord, color: Color) {
        let radius = radius.max(0.0);
        if radius == 0.0 {
            return;
        }
        let diameter = radius * 2.0;
        let top_left = *center - Point::new(radius, radius);
        let circle_bounds = Rect::new(top_left.to_point(), Size::new(diameter, diameter));
        let circle_bounds = circle_bounds.round_out().to_i32();
        if let Some(clipped_rect) = self.bounds.intersection(&circle_bounds) {
            let radius_squared = radius * radius;
            for y in clipped_rect.min_y()..clipped_rect.max_y() {
                let delta_y = y as Coord - center.y;
                let delta_y_2 = delta_y * delta_y;
                for x in clipped_rect.min_x()..clipped_rect.max_x() {
                    let delta_x = x as Coord - center.x;
                    let delta_x_2 = delta_x * delta_x;
                    if delta_x_2 + delta_y_2 < radius_squared {
                        let offset = self.offset_from_x_y(x, y);
                        self.write_color_at_offset(offset as usize, color);
                    }
                }
            }
        }
    }

    fn point_in_circle(x: i32, y: i32, center: &Point, radius_squared: Coord) -> bool {
        let delta_y = y as Coord - center.y;
        let delta_y_2 = delta_y * delta_y;
        let delta_x = x as Coord - center.x;
        let delta_x_2 = delta_x * delta_x;
        delta_x_2 + delta_y_2 < radius_squared
    }

    /// Fill a rounded rectangle with a particular color.
    pub fn fill_roundrect(&mut self, rect: &Rect, corner_radius: Coord, color: Color) {
        let rect_i = rect.round_out().to_i32();
        if let Some(clipped_rect) = self.bounds.intersection(&rect_i) {
            let corner_radius = corner_radius.max(0.0);
            if corner_radius == 0.0 {
                self.fill_rect(rect, color);
                return;
            }
            let center_min_x = rect.min_x() + corner_radius;
            let center_max_x = rect.max_x() - corner_radius;
            let center_min_y = rect.min_y() + corner_radius;
            let center_max_y = rect.max_y() - corner_radius;
            let top_left = Point::new(center_min_x, center_min_y);
            let bottom_left = Point::new(center_min_x, center_max_y);
            let top_right = Point::new(center_max_x, center_min_y);
            let bottom_right = Point::new(center_max_x, center_max_y);
            let corner_radius2 = corner_radius * corner_radius;
            for y in clipped_rect.min_y()..clipped_rect.max_y() {
                let y_f = y as Coord;
                for x in clipped_rect.min_x()..clipped_rect.max_x() {
                    let x_f = x as Coord;
                    let offset = self.offset_from_x_y(x, y);
                    if x_f < center_min_x && y_f < center_min_y {
                        if Self::point_in_circle(x, y, &top_left, corner_radius2) {
                            self.write_color_at_offset(offset as usize, color);
                        }
                    } else if x_f > center_max_x && y_f < center_min_y {
                        if Self::point_in_circle(x, y, &top_right, corner_radius2) {
                            self.write_color_at_offset(offset as usize, color);
                        }
                    } else if x_f < center_min_x && y_f > center_max_y {
                        if Self::point_in_circle(x, y, &bottom_left, corner_radius2) {
                            self.write_color_at_offset(offset as usize, color);
                        }
                    } else if x_f > center_max_x && y_f > center_max_y {
                        if Self::point_in_circle(x, y, &bottom_right, corner_radius2) {
                            self.write_color_at_offset(offset as usize, color);
                        }
                    } else {
                        self.write_color_at_offset(offset as usize, color);
                    }
                }
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
/// Returns the measured width and height.
pub fn measure_text(text: &str, font: &FontDescription) -> Size {
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

#[cfg(test)]
mod tests {
    use crate::{Canvas, Color, IntSize, PixelSink, Point, Rect, Size};
    use fuchsia_framebuffer::{Config, PixelFormat};
    use std::collections::HashSet;

    struct TestPixelSink {
        pub max_offset: usize,
        pub touched_offsets: HashSet<usize>,
    }

    impl TestPixelSink {
        pub fn new(size: IntSize) -> TestPixelSink {
            Self {
                max_offset: (size.width * size.height) as usize * 4,
                touched_offsets: HashSet::new(),
            }
        }
    }

    impl PixelSink for TestPixelSink {
        fn write_pixel_at_offset(&mut self, offset: usize, _value: &[u8]) {
            assert!(
                offset < self.max_offset,
                "attempted to write pixel at an offset {} exceeding limit of {}",
                offset,
                self.max_offset
            );
            self.touched_offsets.insert(offset);
        }
    }

    fn make_test_canvas() -> Canvas<TestPixelSink> {
        const WIDTH: i32 = 800;
        const HEIGHT: i32 = 600;
        let config = Config {
            display_id: 0,
            width: WIDTH as u32,
            height: HEIGHT as u32,
            linear_stride_pixels: WIDTH as u32,
            format: PixelFormat::Argb8888,
            pixel_size_bytes: 4,
        };
        let size = IntSize::new(WIDTH, HEIGHT);
        let sink = TestPixelSink::new(size);
        Canvas::new_with_sink(
            size,
            sink,
            config.linear_stride_bytes() as u32,
            config.pixel_size_bytes,
        )
    }

    #[test]
    fn test_draw_empty_rects() {
        let mut canvas = make_test_canvas();
        let r = Rect::new(Point::new(0.0, 0.0), Size::new(0.0, 0.0));
        let color = Color::from_hash_code("#EBD5B3").expect("color failed to parse");
        canvas.fill_rect(&r, color);
        assert!(canvas.pixel_sink.touched_offsets.is_empty(), "Expected no pixles touched");
    }

    #[test]
    fn test_draw_out_of_bounds() {
        let mut canvas = make_test_canvas();
        let color = Color::from_hash_code("#EBD5B3").expect("color failed to parse");
        let r = Rect::new(Point::new(10000.0, 10000.0), Size::new(20.0, 20.0));
        canvas.fill_roundrect(&r, 4.0, color);
        canvas.fill_circle(&Point::new(20000.0, -10000.0), 204.0, color);
        assert!(canvas.pixel_sink.touched_offsets.is_empty(), "Expected no pixles touched");
        let r = Rect::new(Point::new(-10.0, -10.0), Size::new(20.0, 20.0));
        canvas.fill_rect(&r, color);
        assert!(canvas.pixel_sink.touched_offsets.len() > 0, "Expected some pixles touched");
    }
}
