// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Functions for drawing in Carnelian
//! Carnelian uses the Render abstraction over Mold and Spinel
//! to put pixels on screen. The items in this module are higher-
//! level drawing primitives.

use crate::{
    color::Color,
    geometry::{Coord, Corners, Point, Rect, Size},
    render::{Context as RenderContext, Path, PathBuilder, Raster, RasterBuilder},
};
use anyhow::{anyhow, Context, Error};
use euclid::{
    default::{Box2D, Size2D, Transform2D, Vector2D},
    point2, vec2, Angle,
};
use fuchsia_zircon::{self as zx};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, fs::File, path::PathBuf, slice, str::FromStr};
use ttf_parser::Face;

/// Some Fuchsia device displays are mounted rotated. This value represents
/// The supported rotations and can be used by views to rotate their content
/// to display appropriately when running on the frame buffer.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[allow(missing_docs)]
pub enum DisplayRotation {
    Deg0,
    Deg90,
    Deg180,
    Deg270,
}

impl DisplayRotation {
    /// Create a transformation to accommodate the screen rotation.
    pub fn transform(&self, target_size: &Size2D<Coord>) -> Transform2D<Coord> {
        let w = target_size.width;
        let h = target_size.height;
        match self {
            Self::Deg0 => Transform2D::identity(),
            Self::Deg90 => Transform2D::from_array([0.0, -1.0, 1.0, 0.0, 0.0, h]),
            Self::Deg180 => Transform2D::from_array([-1.0, 0.0, 0.0, -1.0, w, h]),
            Self::Deg270 => Transform2D::from_array([0.0, 1.0, -1.0, 0.0, w, 0.0]),
        }
    }

    /// Create a transformation to undo the screen rotation.
    pub fn inv_transform(&self, target_size: &Size2D<Coord>) -> Transform2D<Coord> {
        let w = target_size.width;
        let h = target_size.height;
        match self {
            Self::Deg0 => Transform2D::identity(),
            Self::Deg90 => Transform2D::from_array([0.0, 1.0, -1.0, 0.0, h, 0.0]),
            Self::Deg180 => Transform2D::from_array([-1.0, 0.0, 0.0, -1.0, w, h]),
            Self::Deg270 => Transform2D::from_array([0.0, -1.0, 1.0, 0.0, 0.0, w]),
        }
    }
}

impl Default for DisplayRotation {
    fn default() -> Self {
        Self::Deg0
    }
}

impl From<DisplayRotation> for Angle<Coord> {
    fn from(display_rotation: DisplayRotation) -> Self {
        let degrees = match display_rotation {
            DisplayRotation::Deg0 => 0.0,
            DisplayRotation::Deg90 => 90.0,
            DisplayRotation::Deg180 => 180.0,
            DisplayRotation::Deg270 => 270.0,
        };
        Angle::degrees(degrees)
    }
}

impl FromStr for DisplayRotation {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "0" => Ok(DisplayRotation::Deg0),
            "90" => Ok(DisplayRotation::Deg90),
            "180" => Ok(DisplayRotation::Deg180),
            "270" => Ok(DisplayRotation::Deg270),
            _ => Err(anyhow!("Invalid DisplayRotation {}", s)),
        }
    }
}

/// Create a render path for the specified rectangle.
pub fn path_for_rectangle(bounds: &Rect, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(bounds.origin)
        .line_to(bounds.top_right())
        .line_to(bounds.bottom_right())
        .line_to(bounds.bottom_left())
        .line_to(bounds.origin);
    path_builder.build()
}

/// Create a render path for the specified rounded rectangle.
pub fn path_for_rounded_rectangle(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Path {
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let control_dist = kappa * corner_radius;

    let top_left_arc_start = bounds.origin + vec2(0.0, corner_radius);
    let top_left_arc_end = bounds.origin + vec2(corner_radius, 0.0);
    let top_left_curve_center = bounds.origin + vec2(corner_radius, corner_radius);
    let top_left_p1 = top_left_curve_center + vec2(-corner_radius, -control_dist);
    let top_left_p2 = top_left_curve_center + vec2(-control_dist, -corner_radius);

    let top_right = bounds.top_right();
    let top_right_arc_start = top_right + vec2(-corner_radius, 0.0);
    let top_right_arc_end = top_right + vec2(0.0, corner_radius);
    let top_right_curve_center = top_right + vec2(-corner_radius, corner_radius);
    let top_right_p1 = top_right_curve_center + vec2(control_dist, -corner_radius);
    let top_right_p2 = top_right_curve_center + vec2(corner_radius, -control_dist);

    let bottom_right = bounds.bottom_right();
    let bottom_right_arc_start = bottom_right + vec2(0.0, -corner_radius);
    let bottom_right_arc_end = bottom_right + vec2(-corner_radius, 0.0);
    let bottom_right_curve_center = bottom_right + vec2(-corner_radius, -corner_radius);
    let bottom_right_p1 = bottom_right_curve_center + vec2(corner_radius, control_dist);
    let bottom_right_p2 = bottom_right_curve_center + vec2(control_dist, corner_radius);

    let bottom_left = bounds.bottom_left();
    let bottom_left_arc_start = bottom_left + vec2(corner_radius, 0.0);
    let bottom_left_arc_end = bottom_left + vec2(0.0, -corner_radius);
    let bottom_left_curve_center = bottom_left + vec2(corner_radius, -corner_radius);
    let bottom_left_p1 = bottom_left_curve_center + vec2(-control_dist, corner_radius);
    let bottom_left_p2 = bottom_left_curve_center + vec2(-corner_radius, control_dist);

    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(top_left_arc_start)
        .cubic_to(top_left_p1, top_left_p2, top_left_arc_end)
        .line_to(top_right_arc_start)
        .cubic_to(top_right_p1, top_right_p2, top_right_arc_end)
        .line_to(bottom_right_arc_start)
        .cubic_to(bottom_right_p1, bottom_right_p2, bottom_right_arc_end)
        .line_to(bottom_left_arc_start)
        .cubic_to(bottom_left_p1, bottom_left_p2, bottom_left_arc_end)
        .line_to(top_left_arc_start);
    path_builder.build()
}

/// Create a render path for the specified circle.
pub fn path_for_circle(center: Point, radius: Coord, render_context: &mut RenderContext) -> Path {
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let control_dist = kappa * radius;

    let mut path_builder = render_context.path_builder().expect("path_builder");
    let left = center + vec2(-radius, 0.0);
    let top = center + vec2(0.0, -radius);
    let right = center + vec2(radius, 0.0);
    let bottom = center + vec2(0.0, radius);
    let left_p1 = center + vec2(-radius, -control_dist);
    let left_p2 = center + vec2(-control_dist, -radius);
    let top_p1 = center + vec2(control_dist, -radius);
    let top_p2 = center + vec2(radius, -control_dist);
    let right_p1 = center + vec2(radius, control_dist);
    let right_p2 = center + vec2(control_dist, radius);
    let bottom_p1 = center + vec2(-control_dist, radius);
    let bottom_p2 = center + vec2(-radius, control_dist);
    path_builder
        .move_to(left)
        .cubic_to(left_p1, left_p2, top)
        .cubic_to(top_p1, top_p2, right)
        .cubic_to(right_p1, right_p2, bottom)
        .cubic_to(bottom_p1, bottom_p2, left);
    path_builder.build()
}

fn point_for_segment_index(
    index: usize,
    center: Point,
    radius: Coord,
    segment_angle: f32,
) -> Point {
    let angle = index as f32 * segment_angle;
    let x = radius * angle.cos();
    let y = radius * angle.sin();
    center + vec2(x, y)
}

/// Create a render path for the specified polygon.
pub fn path_for_polygon(
    center: Point,
    radius: Coord,
    segment_count: usize,
    render_context: &mut RenderContext,
) -> Path {
    let segment_angle = (2.0 * std::f32::consts::PI) / segment_count as f32;
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let first_point = point_for_segment_index(0, center, radius, segment_angle);
    path_builder.move_to(first_point);
    for index in 1..segment_count {
        let pt = point_for_segment_index(index, center, radius, segment_angle);
        path_builder.line_to(pt);
    }
    path_builder.line_to(first_point);
    path_builder.build()
}

/// Create a path for knocking out the points of a rectangle, giving it a
/// rounded appearance.
pub fn path_for_corner_knockouts(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Path {
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let control_dist = kappa * corner_radius;

    let top_left = bounds.top_left();
    let top_left_arc_start = bounds.origin + vec2(0.0, corner_radius);
    let top_left_arc_end = bounds.origin + vec2(corner_radius, 0.0);
    let top_left_curve_center = bounds.origin + vec2(corner_radius, corner_radius);
    let top_left_p1 = top_left_curve_center + vec2(-corner_radius, -control_dist);
    let top_left_p2 = top_left_curve_center + vec2(-control_dist, -corner_radius);

    let top_right = bounds.top_right();
    let top_right_arc_start = top_right + vec2(-corner_radius, 0.0);
    let top_right_arc_end = top_right + vec2(0.0, corner_radius);
    let top_right_curve_center = top_right + vec2(-corner_radius, corner_radius);
    let top_right_p1 = top_right_curve_center + vec2(control_dist, -corner_radius);
    let top_right_p2 = top_right_curve_center + vec2(corner_radius, -control_dist);

    let bottom_right = bounds.bottom_right();
    let bottom_right_arc_start = bottom_right + vec2(0.0, -corner_radius);
    let bottom_right_arc_end = bottom_right + vec2(-corner_radius, 0.0);
    let bottom_right_curve_center = bottom_right + vec2(-corner_radius, -corner_radius);
    let bottom_right_p1 = bottom_right_curve_center + vec2(corner_radius, control_dist);
    let bottom_right_p2 = bottom_right_curve_center + vec2(control_dist, corner_radius);

    let bottom_left = bounds.bottom_left();
    let bottom_left_arc_start = bottom_left + vec2(corner_radius, 0.0);
    let bottom_left_arc_end = bottom_left + vec2(0.0, -corner_radius);
    let bottom_left_curve_center = bottom_left + vec2(corner_radius, -corner_radius);
    let bottom_left_p1 = bottom_left_curve_center + vec2(-control_dist, corner_radius);
    let bottom_left_p2 = bottom_left_curve_center + vec2(-corner_radius, control_dist);

    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(top_left)
        .line_to(top_left_arc_start)
        .cubic_to(top_left_p1, top_left_p2, top_left_arc_end)
        .line_to(top_left)
        .move_to(top_right)
        .line_to(top_right_arc_start)
        .cubic_to(top_right_p1, top_right_p2, top_right_arc_end)
        .line_to(top_right)
        .move_to(bottom_right)
        .line_to(bottom_right_arc_start)
        .cubic_to(bottom_right_p1, bottom_right_p2, bottom_right_arc_end)
        .line_to(bottom_right)
        .move_to(bottom_left)
        .line_to(bottom_left_arc_start)
        .cubic_to(bottom_left_p1, bottom_left_p2, bottom_left_arc_end)
        .line_to(bottom_left);
    path_builder.build()
}

/// Create a render path for a fuchsia-style teardrop cursor.
pub fn path_for_cursor(hot_spot: Point, radius: Coord, render_context: &mut RenderContext) -> Path {
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let control_dist = kappa * radius;
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let center = hot_spot + vec2(radius, radius);
    let left = center + vec2(-radius, 0.0);
    let top = center + vec2(0.0, -radius);
    let right = center + vec2(radius, 0.0);
    let bottom = center + vec2(0.0, radius);
    let top_p1 = center + vec2(control_dist, -radius);
    let top_p2 = center + vec2(radius, -control_dist);
    let right_p1 = center + vec2(radius, control_dist);
    let right_p2 = center + vec2(control_dist, radius);
    let bottom_p1 = center + vec2(-control_dist, radius);
    let bottom_p2 = center + vec2(-radius, control_dist);
    path_builder
        .move_to(hot_spot)
        .line_to(top)
        .cubic_to(top_p1, top_p2, right)
        .cubic_to(right_p1, right_p2, bottom)
        .cubic_to(bottom_p1, bottom_p2, left)
        .line_to(hot_spot);
    path_builder.build()
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

/// Load a font from the provided path.
pub fn load_font(path: PathBuf) -> Result<FontFace, Error> {
    let file = File::open(path).context("File::open")?;
    let vmo = fdio::get_vmo_copy_from_file(&file).context("fdio::get_vmo_copy_from_file")?;
    let size = file.metadata()?.len() as usize;
    let root_vmar = fuchsia_runtime::vmar_root_self();
    let address = root_vmar.map(
        0,
        &vmo,
        0,
        size,
        zx::VmarFlags::PERM_READ | zx::VmarFlags::MAP_RANGE | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
    )?;

    let mapped_font_data = unsafe { slice::from_raw_parts(address as *mut u8, size) };
    Ok(FontFace::new(&*mapped_font_data)?)
}

/// Struct containing a font data.
#[derive(Clone)]
pub struct FontFace {
    /// Font.
    pub face: Face<'static>,
}

impl FontFace {
    /// Create a new FontFace.
    pub fn new(data: &'static [u8]) -> Result<FontFace, Error> {
        let face = Face::from_slice(data, 0)?;
        Ok(FontFace { face })
    }

    /// Get the ascent, in pixels, for this font at the specified size.
    pub fn ascent(&self, size: f32) -> f32 {
        let ascent = self.face.ascender();
        self.face
            .units_per_em()
            .and_then(|units_per_em| Some((ascent as f32 / units_per_em as f32) * size))
            .expect("units_per_em")
    }

    /// Get the descent, in pixels, for this font at the specified size.
    pub fn descent(&self, size: f32) -> f32 {
        let descender = self.face.descender();
        self.face
            .units_per_em()
            .and_then(|units_per_em| Some((descender as f32 / units_per_em as f32) * size))
            .expect("units_per_em")
    }

    /// Get the capital height, in pixels, for this font at the specified size.
    pub fn capital_height(&self, size: f32) -> Option<f32> {
        self.face.capital_height().and_then(|capital_height| {
            self.face
                .units_per_em()
                .and_then(|units_per_em| Some((capital_height as f32 / units_per_em as f32) * size))
        })
    }
}

/// Return the size in pixels for the specified text, face and size.
pub fn measure_text_width(face: &FontFace, font_size: f32, text: &str) -> f32 {
    text.chars()
        .filter_map(|c| {
            let glyph_index = face.face.glyph_index(c);
            glyph_index.and_then(|glyph_index| {
                let hor_advance = face
                    .face
                    .glyph_hor_advance(glyph_index)
                    .and_then(|hor_advance| pixel_size(face, font_size, hor_advance as i16))
                    .expect("hor_advance");
                Some(hor_advance)
            })
        })
        .sum()
}

/// Break up text into chunks guaranteed to be no wider than max_width when rendered with
/// face at font_size.
pub fn linebreak_text(face: &FontFace, font_size: f32, text: &str, max_width: f32) -> Vec<String> {
    let chunks: Vec<&str> = text.split_whitespace().collect();
    let space_width = measure_text_width(face, font_size, " ");
    let breaks: Vec<usize> = chunks
        .iter()
        .enumerate()
        .scan(0.0, |width, (index, word)| {
            let word_width = measure_text_width(face, font_size, word);
            let resulting_line_len = *width + word_width + space_width;
            if resulting_line_len > max_width {
                *width = word_width + space_width;
                Some(Some(index))
            } else {
                *width += word_width;
                *width += space_width;
                Some(None)
            }
        })
        .flatten()
        .chain(std::iter::once(chunks.len()))
        .collect();
    let lines: Vec<String> = breaks
        .iter()
        .scan(0, |first_word_index, last_word_index| {
            let first = *first_word_index;
            *first_word_index = *last_word_index;
            let line = &chunks[first..*last_word_index];
            let line_str = String::from(line.join(" "));
            Some(line_str)
        })
        .collect();

    lines
}

fn pixel_size(face: &FontFace, font_size: f32, value: i16) -> Option<f32> {
    face.face
        .units_per_em()
        .and_then(|units_per_em| Some((value as f32 / units_per_em as f32) * font_size))
}

fn scaled_point2(x: f32, y: f32, scale: f32) -> Point {
    point2(x * scale, -y * scale)
}

const ZERO_RECT: ttf_parser::Rect = ttf_parser::Rect { x_max: 0, x_min: 0, y_max: 0, y_min: 0 };

struct GlyphBuilder<'a> {
    scale: f32,
    context: &'a RenderContext,
    path_builder: Option<PathBuilder>,
    raster_builder: RasterBuilder,
}

impl<'a> GlyphBuilder<'a> {
    fn new(scale: f32, context: &'a mut RenderContext) -> Self {
        let path_builder = context.path_builder().expect("path_builder");
        let raster_builder = context.raster_builder().expect("raster_builder");
        Self { scale, context, path_builder: Some(path_builder), raster_builder }
    }

    fn path_builder(&mut self) -> &mut PathBuilder {
        self.path_builder.as_mut().expect("path_builder() PathBuilder")
    }

    fn raster(self) -> Raster {
        self.raster_builder.build()
    }
}

impl ttf_parser::OutlineBuilder for GlyphBuilder<'_> {
    fn move_to(&mut self, x: f32, y: f32) {
        let scale = self.scale;
        self.path_builder().move_to(scaled_point2(x, y, scale));
    }

    fn line_to(&mut self, x: f32, y: f32) {
        let scale = self.scale;
        self.path_builder().line_to(scaled_point2(x, y, scale));
    }

    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        let scale = self.scale;
        self.path_builder().quad_to(scaled_point2(x1, y1, scale), scaled_point2(x, y, scale));
    }

    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        let scale = self.scale;
        self.path_builder().cubic_to(
            scaled_point2(x1, y1, scale),
            scaled_point2(x2, y2, scale),
            scaled_point2(x, y, scale),
        );
    }

    fn close(&mut self) {
        let path_builder = self.path_builder.take().expect("take PathBuilder");
        let path = path_builder.build();
        self.raster_builder.add(&path, None);
        let path_builder = self.context.path_builder().expect("path_builder");
        self.path_builder = Some(path_builder);
    }
}

#[derive(Debug)]
#[allow(missing_docs)]
pub struct Glyph {
    pub raster: Raster,
    pub bounding_box: Rect,
}

impl Glyph {
    #[allow(missing_docs)]
    pub fn new(
        context: &mut RenderContext,
        face: &FontFace,
        size: f32,
        id: Option<ttf_parser::GlyphId>,
    ) -> Self {
        if let Some(id) = id {
            let units_per_em = face.face.units_per_em().expect("units_per_em");
            let scale = size / units_per_em as f32;
            let mut builder = GlyphBuilder::new(scale, context);
            let glyph_bounding_box =
                &face.face.outline_glyph(id, &mut builder).unwrap_or_else(|| ZERO_RECT);
            let min_x = glyph_bounding_box.x_min as f32 * scale;
            let max_y = -glyph_bounding_box.y_min as f32 * scale;
            let max_x = glyph_bounding_box.x_max as f32 * scale;
            let min_y = -glyph_bounding_box.y_max as f32 * scale;
            let bounding_box = Box2D::new(point2(min_x, min_y), point2(max_x, max_y)).to_rect();
            Self { bounding_box, raster: builder.raster() }
        } else {
            let path_builder = context.path_builder().expect("path_builder");
            let path = path_builder.build();
            let mut raster_builder = context.raster_builder().expect("raster_builder");
            raster_builder.add(&path, None);
            let raster = raster_builder.build();
            Self { bounding_box: Rect::zero(), raster }
        }
    }
}

#[derive(Debug)]
#[allow(missing_docs)]
pub struct GlyphMap {
    glyphs: BTreeMap<ttf_parser::GlyphId, Glyph>,
}

impl GlyphMap {
    #[allow(missing_docs)]
    pub fn new() -> Self {
        Self { glyphs: BTreeMap::new() }
    }
}

#[allow(missing_docs)]
pub struct Text {
    pub raster: Raster,
    pub bounding_box: Rect,
}

impl Text {
    #[allow(missing_docs)]
    pub fn new_with_lines(
        context: &mut RenderContext,
        lines: &Vec<String>,
        size: f32,
        face: &FontFace,
        glyph_map: &mut GlyphMap,
    ) -> Self {
        let glyphs = &mut glyph_map.glyphs;
        let mut bounding_box = Rect::zero();
        let mut raster_union = {
            let raster_builder = context.raster_builder().unwrap();
            raster_builder.build()
        };
        let ascent = face.ascent(size);
        let units_per_em = face.face.units_per_em().expect("units_per_em");
        let scale = size / units_per_em as f32;
        let mut y_offset = vec2(0.0, ascent).to_i32();
        for line in lines.iter() {
            let chars = line.chars();
            let mut x: f32 = 0.0;

            for c in chars {
                if let Some(glyph_index) = face.face.glyph_index(c) {
                    let horizontal_advance = face.face.glyph_hor_advance(glyph_index).unwrap_or(0);
                    let w = horizontal_advance as f32 * scale;
                    let position = y_offset + vec2(x, 0.0).to_i32();
                    let glyph = glyphs
                        .entry(glyph_index)
                        .or_insert_with(|| Glyph::new(context, face, size, Some(glyph_index)));
                    if !glyph.bounding_box.is_empty() {
                        // Clone and translate raster.
                        let raster = glyph
                            .raster
                            .clone()
                            .translate(position.cast_unit::<euclid::UnknownUnit>());
                        raster_union = raster_union + raster;

                        // Expand bounding box.
                        let glyph_bounding_box = &glyph.bounding_box.translate(position.to_f32());

                        if bounding_box.is_empty() {
                            bounding_box = *glyph_bounding_box;
                        } else {
                            bounding_box = bounding_box.union(&glyph_bounding_box);
                        }
                    }

                    x += w;
                }
            }
            y_offset += vec2(0, size as i32);
        }

        Self { raster: raster_union, bounding_box }
    }

    #[allow(missing_docs)]
    pub fn new(
        context: &mut RenderContext,
        text: &str,
        size: f32,
        wrap: f32,
        face: &FontFace,
        glyph_map: &mut GlyphMap,
    ) -> Self {
        let lines = linebreak_text(face, size, text, wrap);
        Self::new_with_lines(context, &lines, size, face, glyph_map)
    }
}

/// Struct containing text grid details.
pub struct TextGrid {
    font_size: f32,
    baseline: Vector2D<f32>,
    cell_size: Size,
}

impl TextGrid {
    /// Creates a new text grid.
    pub fn new(cell_size: Size, cell_padding: f32) -> Self {
        let font_size = cell_size.height - cell_padding;
        let baseline = Vector2D::new(0.0, font_size - cell_padding);

        Self { font_size, baseline, cell_size }
    }
}

/// Struct containing data needed to render a text grid cell.
pub struct TextGridCell {
    /// Raster.
    pub raster: Option<Raster>,
}

impl TextGridCell {
    pub fn new(
        context: &mut RenderContext,
        column: usize,
        row: usize,
        c: char,
        grid: &TextGrid,
        face: &FontFace,
        glyph_map: &mut GlyphMap,
    ) -> TextGridCell {
        let raster = if let Some(glyph_index) = face.face.glyph_index(c) {
            let glyphs = &mut glyph_map.glyphs;
            let font_size = grid.font_size;
            let glyph = glyphs
                .entry(glyph_index)
                .or_insert_with(|| Glyph::new(context, face, font_size, Some(glyph_index)));
            if glyph.bounding_box.is_empty() {
                None
            } else {
                let cell_position = Point::new(
                    grid.cell_size.width * column as f32,
                    grid.cell_size.height * row as f32,
                );
                let char_position = cell_position + grid.baseline;

                let raster = glyph.raster.clone().translate(char_position.to_vector().to_i32());

                // Add empty raster to enable caching of the translated glyph.
                // TODO: add more appropriate API for this.
                let empty_raster = {
                    let raster_builder = context.raster_builder().unwrap();
                    raster_builder.build()
                };
                let raster = raster + empty_raster;

                Some(raster)
            }
        } else {
            None
        };

        Self { raster }
    }
}

#[cfg(test)]
mod tests {
    use super::{GlyphMap, Size, Text, TextGrid, TextGridCell};
    use crate::{
        drawing::{DisplayRotation, FontFace},
        render::{
            generic::{self, Backend},
            Context as RenderContext, ContextInner,
        },
    };
    use euclid::{approxeq::ApproxEq, size2, vec2};
    use fuchsia_async::{self as fasync, Time, TimeoutExt};
    use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameUsage};
    use once_cell::sync::Lazy;

    const DEFAULT_TIMEOUT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(5);

    // This font creation method isn't ideal. The correct method would be to ask the Fuchsia
    // font service for the font data.
    static FONT_DATA: &'static [u8] = include_bytes!(
        "../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf"
    );
    static FONT_FACE: Lazy<FontFace> =
        Lazy::new(|| FontFace::new(&FONT_DATA).expect("Failed to create font"));

    #[fasync::run_singlethreaded(test)]
    async fn test_text_bounding_box() {
        let size = size2(800, 800);
        let mut buffer_allocator = BufferCollectionAllocator::new(
            size.width,
            size.height,
            fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
            FrameUsage::Cpu,
            3,
        )
        .expect("BufferCollectionAllocator::new");
        let context_token = buffer_allocator
            .duplicate_token()
            .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
                panic!("Timed out while waiting for duplicate_token")
            })
            .await
            .expect("token");
        let mold_context = generic::Mold::new_context(context_token, size, DisplayRotation::Deg0);
        let _buffers_result = buffer_allocator
            .allocate_buffers(true)
            .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
                panic!("Timed out while waiting for sysmem bufers")
            })
            .await;
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let mut glyphs = GlyphMap::new();
        let text =
            Text::new(&mut render_context, "Good Morning", 20.0, 200.0, &FONT_FACE, &mut glyphs);

        let expected_origin = euclid::point2(0.5371094, 4.765625);
        let expected_size = vec2(132.33594, 19.501953);
        assert!(
            text.bounding_box.origin.approx_eq(&expected_origin),
            "Expected bounding box origin to be close to {:?} but found {:?}",
            expected_origin,
            text.bounding_box.origin
        );
        assert!(
            text.bounding_box.size.to_vector().approx_eq(&expected_size),
            "Expected bounding box origin to be close to {:?} but found {:?}",
            expected_size,
            text.bounding_box.size
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_textgridcell() {
        let size = size2(800, 800);
        let mut buffer_allocator = BufferCollectionAllocator::new(
            size.width,
            size.height,
            fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
            FrameUsage::Cpu,
            3,
        )
        .expect("BufferCollectionAllocator::new");
        let context_token = buffer_allocator
            .duplicate_token()
            .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
                panic!("Timed out while waiting for duplicate_token")
            })
            .await
            .expect("token");
        let mold_context = generic::Mold::new_context(context_token, size, DisplayRotation::Deg0);
        let _buffers_result = buffer_allocator
            .allocate_buffers(true)
            .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
                panic!("Timed out while waiting for sysmem bufers")
            })
            .await;
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let mut glyphs = GlyphMap::new();
        let grid = TextGrid::new(Size::new(16.0, 32.0), 2.0);
        let a_cell =
            TextGridCell::new(&mut render_context, 0, 0, 'a', &grid, &FONT_FACE, &mut glyphs);
        assert!(a_cell.raster.is_some(), "Expected some raster");
    }
}
