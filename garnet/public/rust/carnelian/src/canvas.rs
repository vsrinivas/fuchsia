// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::geometry::{Coord, IntCoord, IntPoint, IntRect, IntSize, Point, Rect, Size};
use anyhow::Error;
use core::ops::Range;
use euclid::rect;
use fidl_fuchsia_ui_gfx::ColorRgba;
use mapped_vmo::Mapping;
use rusttype::{Font, FontCollection, Scale};
use std::sync::Arc;

/// Struct representing an RGBA color value
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Color {
    /// Red
    pub r: u8,
    /// Green
    pub g: u8,
    /// Blue
    pub b: u8,
    /// Alpha
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

impl Color {
    /// Create a new color set to black with full alpha
    pub fn new() -> Color {
        Color { r: 0, g: 0, b: 0, a: 255 }
    }

    /// Create a new color set to white with full alpha
    pub fn white() -> Color {
        Color { r: 255, g: 255, b: 255, a: 255 }
    }

    fn extract_hex_slice(hash_code: &str, start_index: usize) -> Result<u8, Error> {
        Ok(u8::from_str_radix(&hash_code[start_index..start_index + 2], 16)?)
    }

    /// Create a color from a six hexadecimal digit string like '#EBD5B3'
    pub fn from_hash_code(hash_code: &str) -> Result<Color, Error> {
        let mut new_color = Color::new();
        new_color.r = Color::extract_hex_slice(&hash_code, 1)?;
        new_color.g = Color::extract_hex_slice(&hash_code, 3)?;
        new_color.b = Color::extract_hex_slice(&hash_code, 5)?;
        Ok(new_color)
    }

    /// Create a Scenic-compatible [`ColorRgba`]
    pub fn make_color_rgba(&self) -> ColorRgba {
        ColorRgba { red: self.r, green: self.g, blue: self.b, alpha: self.a }
    }

    /// Create a 565 array for this color
    pub fn to_565(&self) -> [u8; 2] {
        let pixel = [self.r, self.g, self.b, self.a];
        to_565(&pixel)
    }
}

/// Struct combining a foreground and background color.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Paint {
    /// Color for foreground painting
    pub fg: Color,
    /// Color for background painting
    pub bg: Color,
}

impl Paint {
    /// Create a paint from a pair of hash codes
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
    /// Font.
    pub font: Font<'a>,
}

/// Struct containing font, size and baseline.
#[allow(missing_docs)]
pub struct FontDescription<'a, 'b> {
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

#[derive(Debug, Clone)]
/// Struct representing one row of an update area. Right now
/// it only handles a contiguous row of pixels, but it could
/// be expanded to handle multiple discontiguous ranges of
/// pixels if that turns out to be a common enough case
/// to be worth the complexity.
enum UpdateAreaRow {
    Empty,
    Single(Range<IntCoord>),
}

impl Default for UpdateAreaRow {
    fn default() -> Self {
        UpdateAreaRow::Empty
    }
}

impl UpdateAreaRow {
    #[cfg(test)]
    pub fn covered_pixels(&self) -> IntCoord {
        match self {
            UpdateAreaRow::Empty => 0,
            UpdateAreaRow::Single(range) => range.end - range.start,
        }
    }
}

#[derive(Debug, Clone)]
struct UpdateArea {
    bounds: IntRect,
    rows: Vec<UpdateAreaRow>,
}

impl UpdateArea {
    /// Create an update area for the specified bounds.
    fn new(bounds: &IntRect) -> UpdateArea {
        let row_count = bounds.size.height.max(0) as usize;
        let mut update_area = UpdateArea { bounds: *bounds, rows: Vec::with_capacity(row_count) };
        update_area.rows.resize(row_count, Default::default());
        update_area
    }

    /// Creator function for the default case where the update area
    /// is the entire bounds. Useful for cases where the Canvas user
    /// is not interested in maintaining update areas.
    fn new_with_bounds_patch(bounds: &IntRect) -> UpdateArea {
        let mut update_area = UpdateArea::new(bounds);
        update_area.add_patch(bounds);
        update_area
    }

    /// Adds a rectangular patch to the area of the canvas where
    /// drawing will be allowed to change pixels.
    fn add_patch(&mut self, patch: &IntRect) {
        if let Some(clipped_patch) = self.bounds.intersection(patch) {
            for y in clipped_patch.min_y()..clipped_patch.max_y() {
                let index = y as usize;
                let row = &mut self.rows[index];
                match row {
                    UpdateAreaRow::Empty => {
                        *row = UpdateAreaRow::Single(clipped_patch.min_x()..clipped_patch.max_x());
                    }
                    UpdateAreaRow::Single(range) => {
                        let left = clipped_patch.min_x().min(range.start);
                        let right = clipped_patch.max_x().max(range.end);
                        *row = UpdateAreaRow::Single(left..right);
                    }
                }
            }
        }
    }

    /// Resets the update area to empty, allowing no drawing.
    fn reset(&mut self) {
        self.rows.iter_mut().for_each(|a| *a = Default::default());
    }

    #[cfg(test)]
    /// Function used for unit tests
    fn covered_pixels(&self) -> i32 {
        self.rows.iter().map(UpdateAreaRow::covered_pixels).sum()
    }
}

#[derive(Debug)]
struct UpdateAreaIter<'a> {
    area: &'a UpdateArea,
    target: IntRect,
    y: IntCoord,
    y_limit: IntCoord,
}

impl<'a> UpdateAreaIter<'a> {
    fn new(area: &'a UpdateArea, target: &IntRect) -> UpdateAreaIter<'a> {
        UpdateAreaIter { area, target: *target, y: target.min_y(), y_limit: target.max_y() }
    }
}

impl<'a> Iterator for UpdateAreaIter<'a> {
    type Item = (IntCoord, Range<IntCoord>);

    fn next(&mut self) -> Option<Self::Item> {
        while self.y < self.y_limit {
            let y = self.y;
            self.y += 1;
            let row = &self.area.rows[y as usize];
            match row {
                UpdateAreaRow::Empty => (),
                UpdateAreaRow::Single(range) => {
                    let target_range = self.target.min_x()..self.target.max_x();
                    if range.contains(&target_range.start) || target_range.contains(&range.start) {
                        let min_x = target_range.start.max(range.start);
                        let max_x = target_range.end.min(range.end);
                        if min_x != max_x {
                            return Some((y, min_x..max_x));
                        }
                    }
                }
            }
        }
        None
    }
}

#[cfg(test)]
mod update_area_tests {
    use super::{UpdateArea, UpdateAreaIter};
    use crate::{IntCoord, IntPoint, IntRect, IntSize};
    use core::ops::Range;
    use euclid::rect;
    use itertools::assert_equal;

    struct UpdateAreaExpectedIter {
        expected: Box<dyn Iterator<Item = (IntCoord, Range<IntCoord>)>>,
    }

    impl UpdateAreaExpectedIter {
        pub fn new_from_rect(rect: &IntRect) -> UpdateAreaExpectedIter {
            let mut expected = Vec::new();
            for y in rect.min_y()..rect.max_y() {
                expected.push((y, rect.min_x()..rect.max_x()));
            }
            UpdateAreaExpectedIter { expected: Box::new(expected.into_iter()) }
        }

        pub fn new_from_rects(rects: &[IntRect]) -> UpdateAreaExpectedIter {
            let mut expected = Vec::new();
            for rect in rects {
                for y in rect.min_y()..rect.max_y() {
                    expected.push((y, rect.min_x()..rect.max_x()));
                }
            }
            UpdateAreaExpectedIter { expected: Box::new(expected.into_iter()) }
        }
    }

    impl Iterator for UpdateAreaExpectedIter {
        type Item = (IntCoord, Range<IntCoord>);

        fn next(&mut self) -> Option<Self::Item> {
            self.expected.next()
        }
    }

    fn update_area_test_bounds() -> IntRect {
        IntRect::new(IntPoint::zero(), IntSize::new(800, 600))
    }

    #[test]
    fn test_update_area() {
        let bounds = update_area_test_bounds();
        let mut area = UpdateArea::new(&bounds);
        let covered_pixels = area.covered_pixels();
        let expected_covered_pixels = 0;
        assert_eq!(
            covered_pixels, expected_covered_pixels,
            "Expected {} pixels covered, got {}",
            expected_covered_pixels, covered_pixels
        );

        let patch_bounds = rect(30, 30, 10, 10);
        area.add_patch(&patch_bounds);
        let covered_pixels = area.covered_pixels();
        assert_eq!(
            patch_bounds.area(),
            covered_pixels,
            "Expected {} pixels covered, got {}",
            patch_bounds.area(),
            covered_pixels,
        );

        let patch_bounds2 = rect(10, 10, 10, 10);
        area.add_patch(&patch_bounds2);
        let patch_bounds3 = rect(-10, -10, 20, 20);
        area.add_patch(&patch_bounds3);
        let covered_pixels = area.covered_pixels();
        let expected_covered_pixels = 300;
        assert_eq!(
            expected_covered_pixels, covered_pixels,
            "Expected {} pixels covered, got {}",
            expected_covered_pixels, covered_pixels,
        );
    }

    #[test]
    fn test_update_area_iter() {
        let bounds = update_area_test_bounds();
        let mut area = UpdateArea::new(&bounds);
        let patch = rect(20, 20, 80, 80);
        area.add_patch(&patch);
        let target = rect(30, 30, 20, 20);
        let area_iter = UpdateAreaIter::new(&area, &target);
        let overlapping = patch.intersection(&target).expect("bounds and target should overlap");
        let expected_are_iter = UpdateAreaExpectedIter::new_from_rect(&overlapping);
        assert_equal(area_iter, expected_are_iter);
    }

    #[test]
    fn test_update_area_iter_multiple_patch() {
        let bounds = update_area_test_bounds();
        let mut area = UpdateArea::new(&bounds);
        let patch = rect(20, 20, 80, 80);
        let patch2 = rect(100, 100, 80, 80);
        area.add_patch(&patch);
        area.add_patch(&patch2);

        let area_iter = UpdateAreaIter::new(&area, &bounds);
        let overlapping1 = patch.intersection(&bounds).expect("bounds and target should overlap");
        let overlapping2 = patch2.intersection(&bounds).expect("bounds and target should overlap");
        let expected_are_iter =
            UpdateAreaExpectedIter::new_from_rects(&[overlapping1, overlapping2]);
        assert_equal(area_iter, expected_are_iter);
    }

    #[test]
    fn test_update_area_iter_multiple_patch_same_row() {
        let bounds = update_area_test_bounds();
        let mut area = UpdateArea::new(&bounds);
        let patch = rect(20, 20, 80, 80);
        let patch2 = rect(100, 20, 80, 80);
        area.add_patch(&patch);
        area.add_patch(&patch2);

        let area_iter = UpdateAreaIter::new(&area, &bounds);
        let combined = patch.union(&patch2);
        let expected_are_iter = UpdateAreaExpectedIter::new_from_rects(&[combined]);
        assert_equal(area_iter, expected_are_iter);
    }
}

/// Trait abstracting a target to which pixels can be written. Only used currently
/// for testing.
pub trait PixelSink: Clone + Send + Sync {
    /// Write an RGBA pixel at a byte offset within the sink.
    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]);
}

/// Pixel sink targeting a shared buffer.
#[derive(Clone)]
pub struct MappingPixelSink {
    mapping: Arc<Mapping>,
}

impl MappingPixelSink {
    /// Make a new MappingPixelSink for the mapped VMO.
    pub fn new(mapping: &Arc<Mapping>) -> MappingPixelSink {
        MappingPixelSink { mapping: mapping.clone() }
    }
}

impl PixelSink for MappingPixelSink {
    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]) {
        self.mapping.write_at(offset, &value);
    }
}

/// Canvas is used to do simple graphics and text rendering into a
/// SharedBuffer that can then be displayed using Scenic or
/// Display Manager.
#[allow(missing_docs)]
pub struct Canvas<T: PixelSink> {
    // Assumes a pixel format of BGRA8 and a color space of sRGB.
    pub pixel_sink: T,
    pub row_stride: u32,
    pub col_stride: u32,
    pub id: u64,
    pub index: u32,
    bounds: IntRect,
    current_clip: Option<IntRect>,
    update_area: UpdateArea,
}

impl<T: PixelSink> Canvas<T> {
    /// Create a canvas targeting a shared buffer with stride.
    pub fn new(
        size: IntSize,
        pixel_sink: T,
        row_stride: u32,
        col_stride: u32,
        id: u64,
        index: u32,
    ) -> Self {
        let bounds = IntRect::new(IntPoint::zero(), size);
        Canvas {
            pixel_sink,
            row_stride,
            col_stride,
            id,
            index,
            bounds: bounds,
            current_clip: None,
            update_area: UpdateArea::new_with_bounds_patch(&bounds),
        }
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

    fn clipped_target_rect(&self, rect: &Rect) -> Option<IntRect> {
        let i_rect = rect.round_out().to_i32();
        if let Some(clipping_rect) = self.current_clip {
            clipping_rect.intersection(&i_rect)
        } else {
            self.bounds.intersection(&i_rect)
        }
    }

    /// reset_update_area
    pub fn reset_update_area(&mut self) {
        self.update_area.reset();
    }

    /// add to update area
    pub fn add_to_update_area(&mut self, rect: &Rect) {
        let i_rect = rect.round_out().to_i32();
        self.update_area.add_patch(&i_rect);
    }

    /// Fill a rectangle with a particular color.
    pub fn fill_rect(&mut self, rect: &Rect, color: Color) {
        if rect.is_empty() {
            return;
        }
        let clipped_target = self.clipped_target_rect(&rect);
        if let Some(clipped_rect) = clipped_target {
            let area = self.update_area.clone();
            let iter = UpdateAreaIter::new(&area, &clipped_rect);
            for (y, row_iter) in iter {
                for x in row_iter {
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
        let radius_squared = radius * radius;
        let top_left = *center - Point::new(radius, radius);
        let circle_bounds = Rect::new(top_left.to_point(), Size::new(diameter, diameter));
        let clipped_target = self.clipped_target_rect(&circle_bounds);
        if let Some(clipped_rect) = clipped_target {
            let area = self.update_area.clone();
            let iter = UpdateAreaIter::new(&area, &clipped_rect);
            for (y, row_iter) in iter {
                let delta_y = y as Coord - center.y;
                let delta_y_2 = delta_y * delta_y;
                for x in row_iter {
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
        let clipped_target = self.clipped_target_rect(&rect);
        if let Some(clipped_rect) = clipped_target {
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
            let area = self.update_area.clone();
            let iter = UpdateAreaIter::new(&area, &clipped_rect);
            for (y, row_iter) in iter {
                let y_f = y as Coord;
                for x in row_iter {
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
        font: &mut FontDescription<'_, '_>,
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
        font: &mut FontDescription<'_, '_>,
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

    /// Reduces the clip region to the intersection of the current clip and the given rectangle.
    pub fn intersect_clip_with_rect(&mut self, rect: &Rect) {
        let rect_i = rect.round_in().to_i32();
        if let Some(clipped_rect) = rect_i.intersection(&self.bounds) {
            if self.current_clip.is_none() {
                self.current_clip = Some(clipped_rect);
            } else {
                panic!("unhandled case");
            }
        } else {
            self.current_clip = Some(IntRect::zero());
        }
    }
}

/// Measure a line of text `text` and with the typographic characteristics in `font`.
/// Returns the measured width and height.
pub fn measure_text(text: &str, font: &FontDescription<'_, '_>) -> Size {
    if font.size == 0 {
        return Size::zero();
    }
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
    use crate::{
        label::make_font_description, measure_text, Canvas, Color, Coord, IntSize, PixelSink,
        Point, Rect, Size,
    };
    use fuchsia_framebuffer::{Config, PixelFormat};
    use std::collections::HashSet;

    fn test_canvas_size() -> IntSize {
        IntSize::new(800, 600)
    }

    #[derive(Clone)]
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

    fn make_test_canvas(size: IntSize) -> Canvas<TestPixelSink> {
        let config = Config {
            display_id: 0,
            width: size.width as u32,
            height: size.height as u32,
            linear_stride_bytes: (size.width * 4) as u32,
            format: PixelFormat::Argb8888,
            pixel_size_bytes: 4,
        };
        let sink = TestPixelSink::new(size);
        Canvas::new(size, sink, config.linear_stride_bytes() as u32, config.pixel_size_bytes, 0, 0)
    }

    #[test]
    fn test_draw_empty_rects() {
        let mut canvas = make_test_canvas(test_canvas_size());
        let r = Rect::new(Point::new(0.0, 0.0), Size::new(0.0, 0.0));
        let color = Color::from_hash_code("#EBD5B3").expect("color failed to parse");
        canvas.fill_rect(&r, color);
        assert!(canvas.pixel_sink.touched_offsets.is_empty(), "Expected no pixels touched");
    }

    #[test]
    fn test_draw_out_of_bounds() {
        let mut canvas = make_test_canvas(test_canvas_size());
        let color = Color::from_hash_code("#EBD5B3").expect("color failed to parse");
        let r = Rect::new(Point::new(10000.0, 10000.0), Size::new(20.0, 20.0));
        canvas.fill_roundrect(&r, 4.0, color);
        canvas.fill_circle(&Point::new(20000.0, -10000.0), 204.0, color);
        assert!(canvas.pixel_sink.touched_offsets.is_empty(), "Expected no pixels touched");
        let r = Rect::new(Point::new(-10.0, -10.0), Size::new(20.0, 20.0));
        canvas.fill_rect(&r, color);
        assert!(canvas.pixel_sink.touched_offsets.len() > 0, "Expected some pixels touched");
    }

    #[test]
    fn test_clip() {
        let canvas_size = test_canvas_size();
        let mut canvas = make_test_canvas(canvas_size);
        let top_left = Rect::new(Point::new(20.0, 20.0), Size::new(20.0, 20.0));
        canvas.intersect_clip_with_rect(&top_left);
        let fill_rect = Rect::new(Point::new(0.0, 0.0), canvas_size.to_f32());
        let color = Color::from_hash_code("#EBD5B3").expect("color failed to parse");
        canvas.fill_rect(&fill_rect, color);
        let expected_pixels = top_left.to_usize().area();
        assert_eq!(
            canvas.pixel_sink.touched_offsets.len(),
            expected_pixels,
            "Expected {} pixels touched, got {}",
            expected_pixels,
            canvas.pixel_sink.touched_offsets.len()
        );

        let mut canvas = make_test_canvas(canvas_size);
        let out_of_bounds = Rect::new(
            Point::new(canvas_size.width as Coord + 2000.0, canvas_size.height as Coord + 2000.0),
            Size::new(20.0, 20.0),
        );
        canvas.intersect_clip_with_rect(&out_of_bounds);
        canvas.fill_rect(&fill_rect, color);
        assert_eq!(
            canvas.pixel_sink.touched_offsets.len(),
            0,
            "Expected 0 pixels touched, got {}",
            canvas.pixel_sink.touched_offsets.len()
        );
    }

    #[test]
    fn test_measure_font_size_zero() {
        let font = make_font_description(0, 0);
        assert_eq!(measure_text("hello", &font), Size::zero());
    }
}
