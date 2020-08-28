// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Functions for drawing in Carnelian
//! Carnelian uses the Render abstraction over Mold and Spinel
//! to put pixels on screen. The items in this module are higher-
//! level drawing primitives.

use crate::{
    color::Color,
    geometry::{Coord, Corners, Point, Rect},
    render::{Context as RenderContext, Path, Raster},
};
use anyhow::Error;
use euclid::{
    default::{Box2D, Size2D, Transform2D, Vector2D},
    vec2, Angle,
};
use rusttype::{Font, FontCollection, GlyphId, Scale, Segment};
use std::collections::BTreeMap;
use textwrap::wrap_iter;
/// Some Fuchsia device displays are mounted rotated. This value represents
/// The supported rotations and can be used by views to rotate their content
/// to display appropriately when running on the frame buffer.
#[derive(Debug, Clone, Copy)]
pub enum DisplayRotation {
    Deg0,
    Deg90,
    Deg180,
    Deg270,
}

impl DisplayRotation {
    pub fn transform(&self, target_size: &Size2D<Coord>) -> Option<Transform2D<Coord>> {
        let post_translation = match self {
            DisplayRotation::Deg90 => Vector2D::new(0.0, target_size.width),
            DisplayRotation::Deg180 => Vector2D::new(target_size.width, target_size.height),
            DisplayRotation::Deg270 => Vector2D::new(target_size.height, 0.0),
            DisplayRotation::Deg0 => Vector2D::zero(),
        };
        self.rotation().and_then(|transform| Some(transform.post_translate(post_translation)))
    }

    pub fn rotation(&self) -> Option<Transform2D<Coord>> {
        match self {
            DisplayRotation::Deg0 => None,
            _ => {
                let display_rotation = *self;
                let angle: Angle<Coord> = display_rotation.into();
                Some(Transform2D::create_rotation(angle))
            }
        }
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

    let top_left_arc_start = bounds.origin + Vector2D::new(0.0, corner_radius);
    let top_left_arc_end = bounds.origin + Vector2D::new(corner_radius, 0.0);
    let top_left_curve_center = bounds.origin + Vector2D::new(corner_radius, corner_radius);
    let top_left_p1 = top_left_curve_center + Vector2D::new(-corner_radius, -control_dist);
    let top_left_p2 = top_left_curve_center + Vector2D::new(-control_dist, -corner_radius);

    let top_right = bounds.top_right();
    let top_right_arc_start = top_right + Vector2D::new(-corner_radius, 0.0);
    let top_right_arc_end = top_right + Vector2D::new(0.0, corner_radius);
    let top_right_curve_center = top_right + Vector2D::new(-corner_radius, corner_radius);
    let top_right_p1 = top_right_curve_center + Vector2D::new(control_dist, -corner_radius);
    let top_right_p2 = top_right_curve_center + Vector2D::new(corner_radius, -control_dist);

    let bottom_right = bounds.bottom_right();
    let bottom_right_arc_start = bottom_right + Vector2D::new(0.0, -corner_radius);
    let bottom_right_arc_end = bottom_right + Vector2D::new(-corner_radius, 0.0);
    let bottom_right_curve_center = bottom_right + Vector2D::new(-corner_radius, -corner_radius);
    let bottom_right_p1 = bottom_right_curve_center + Vector2D::new(corner_radius, control_dist);
    let bottom_right_p2 = bottom_right_curve_center + Vector2D::new(control_dist, corner_radius);

    let bottom_left = bounds.bottom_left();
    let bottom_left_arc_start = bottom_left + Vector2D::new(corner_radius, 0.0);
    let bottom_left_arc_end = bottom_left + Vector2D::new(0.0, -corner_radius);
    let bottom_left_curve_center = bottom_left + Vector2D::new(corner_radius, -corner_radius);
    let bottom_left_p1 = bottom_left_curve_center + Vector2D::new(-control_dist, corner_radius);
    let bottom_left_p2 = bottom_left_curve_center + Vector2D::new(-corner_radius, control_dist);

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
    let left = center + Vector2D::new(-radius, 0.0);
    let top = center + Vector2D::new(0.0, -radius);
    let right = center + Vector2D::new(radius, 0.0);
    let bottom = center + Vector2D::new(0.0, radius);
    let left_p1 = center + Vector2D::new(-radius, -control_dist);
    let left_p2 = center + Vector2D::new(-control_dist, -radius);
    let top_p1 = center + Vector2D::new(control_dist, -radius);
    let top_p2 = center + Vector2D::new(radius, -control_dist);
    let right_p1 = center + Vector2D::new(radius, control_dist);
    let right_p2 = center + Vector2D::new(control_dist, radius);
    let bottom_p1 = center + Vector2D::new(-control_dist, radius);
    let bottom_p2 = center + Vector2D::new(-radius, control_dist);
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
    center + Vector2D::new(x, y)
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

#[derive(Debug)]
pub struct Glyph {
    pub raster: Raster,
    pub bounding_box: Rect,
    pub display_bounding_box: Rect,
}

impl Glyph {
    pub fn new(context: &mut RenderContext, face: &FontFace<'_>, size: f32, id: GlyphId) -> Self {
        Self::new_rotated(context, face, size, id, DisplayRotation::Deg0)
    }

    pub fn new_rotated(
        context: &mut RenderContext,
        face: &FontFace<'_>,
        size: f32,
        id: GlyphId,
        display_rotation: DisplayRotation,
    ) -> Self {
        let mut path_builder = context.path_builder().expect("path_builder");
        let mut bounding_box = Box2D::zero();
        let scale = Scale::uniform(size);

        macro_rules! flip_y {
            ( $p:expr ) => {
                Point::new($p.x, -$p.y)
            };
        }

        let glyph = face.font.glyph(id).scaled(scale);
        if let Some(glyph_box) = glyph.exact_bounding_box() {
            let contours = glyph.shape().expect("shape");
            for contour in contours {
                for segment in &contour.segments {
                    match segment {
                        Segment::Line(line) => {
                            path_builder.move_to(flip_y!(line.p[0]));
                            path_builder.line_to(flip_y!(line.p[1]));
                        }
                        Segment::Curve(curve) => {
                            let p0 = flip_y!(curve.p[0]);
                            let p1 = flip_y!(curve.p[1]);
                            let p2 = flip_y!(curve.p[2]);

                            path_builder.move_to(p0);
                            // TODO: use quad_to when working correctly in spinel backend.
                            path_builder.cubic_to(
                                p0.lerp(p1, 2.0 / 3.0),
                                p2.lerp(p1, 2.0 / 3.0),
                                p2,
                            );
                        }
                    }
                }
            }

            bounding_box = bounding_box.union(&Box2D::new(
                Point::new(glyph_box.min.x, glyph_box.min.y),
                Point::new(glyph_box.max.x, glyph_box.max.y),
            ));
        }

        let bounding_box = bounding_box.to_rect();
        let transform = display_rotation.transform(&Size2D::zero());
        let display_bounding_box = if let Some(transform) = transform {
            transform.transform_rect(&bounding_box)
        } else {
            bounding_box
        };
        let path = path_builder.build();
        let mut raster_builder = context.raster_builder().expect("raster_builder");
        let rotation_transform =
            display_rotation.rotation().and_then(|transform| Some(transform.to_untyped()));
        raster_builder.add(&path, rotation_transform.as_ref());

        Self { raster: raster_builder.build(), display_bounding_box, bounding_box }
    }
}

#[derive(Debug)]
pub struct GlyphMap {
    glyphs: BTreeMap<GlyphId, Glyph>,
    rotation: DisplayRotation,
}

impl GlyphMap {
    pub fn new() -> Self {
        Self::new_with_rotation(DisplayRotation::Deg0)
    }

    pub fn new_with_rotation(rotation: DisplayRotation) -> Self {
        Self { glyphs: BTreeMap::new(), rotation }
    }
}

pub struct Text {
    pub raster: Raster,
    pub bounding_box: Rect,
    pub display_bounding_box: Rect,
}

impl Text {
    pub fn new(
        context: &mut RenderContext,
        text: &str,
        size: f32,
        wrap: usize,
        face: &FontFace<'_>,
        glyph_map: &mut GlyphMap,
    ) -> Self {
        let glyphs = &mut glyph_map.glyphs;
        let display_rotation = glyph_map.rotation;
        let mut display_bounding_box = Rect::zero();
        let scale = Scale::uniform(size);
        let v_metrics = face.font.v_metrics(scale);
        let mut ascent = v_metrics.ascent;
        let mut raster_union = None;
        let transform = glyph_map
            .rotation
            .transform(&Size2D::new(0.0, ascent))
            .unwrap_or(Transform2D::identity());

        for line in wrap_iter(text, wrap) {
            // TODO: adjust vertical alignment of glyphs to match first glyph.
            let y_offset = transform.transform_vector(Vector2D::new(0.0, ascent)).to_i32();
            let chars = line.chars();
            let mut x: f32 = 0.0;
            let mut last = None;
            for g in face.font.glyphs_for(chars) {
                let g = g.scaled(scale);
                let id = g.id();
                let w = g.h_metrics().advance_width
                    + last.map(|last| face.font.pair_kerning(scale, last, id)).unwrap_or(0.0);

                // Lookup glyph entry in cache.
                // TODO: improve sub pixel placement using a larger cache.
                let position =
                    y_offset + transform.transform_vector(Vector2D::new(x, 0.0)).to_i32();
                let glyph = glyphs.entry(id).or_insert_with(|| {
                    Glyph::new_rotated(context, face, size, id, display_rotation)
                });

                // Clone and translate raster.
                let raster =
                    glyph.raster.clone().translate(position.cast_unit::<euclid::UnknownUnit>());
                raster_union = if let Some(raster_union) = raster_union {
                    Some(raster_union + raster)
                } else {
                    Some(raster)
                };

                // Expand bounding box.
                let glyph_bounding_box = &glyph.display_bounding_box.translate(position.to_f32());

                if display_bounding_box.is_empty() {
                    display_bounding_box = *glyph_bounding_box;
                } else {
                    display_bounding_box = display_bounding_box.union(&glyph_bounding_box);
                }

                x += w;
                last = Some(id);
            }
            ascent += size;
        }

        let transform = display_rotation.transform(&Size2D::zero());
        let bounding_box = if let Some(transform) = transform {
            transform.inverse().expect("inverse").transform_rect(&display_bounding_box)
        } else {
            display_bounding_box.cast_unit::<euclid::UnknownUnit>()
        };

        Self { raster: raster_union.expect("raster_union"), bounding_box, display_bounding_box }
    }
}

#[cfg(test)]
mod tests {
    use super::{GlyphMap, Text};
    use crate::{
        drawing::FontFace,
        geometry::{Point, UintSize},
        render::{
            generic::{self, Backend},
            Context as RenderContext, ContextInner,
        },
    };
    use euclid::{approxeq::ApproxEq, Vector2D};
    use fuchsia_async::{self as fasync, Time, TimeoutExt};
    use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameUsage};
    use lazy_static::lazy_static;

    const DEFAULT_TIMEOUT: fuchsia_zircon::Duration = fuchsia_zircon::Duration::from_seconds(5);

    // This font creation method isn't ideal. The correct method would be to ask the Fuchsia
    // font service for the font data.
    static FONT_DATA: &'static [u8] = include_bytes!(
        "../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf"
    );

    lazy_static! {
        pub static ref FONT_FACE: FontFace<'static> =
            FontFace::new(&FONT_DATA).expect("Failed to create font");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_text_bounding_box() {
        let size = UintSize::new(800, 800);
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
        let mold_context = generic::Mold::new_context(context_token, size);
        let _buffers_result = buffer_allocator
            .allocate_buffers(true)
            .on_timeout(Time::after(DEFAULT_TIMEOUT), || {
                panic!("Timed out while waiting for sysmem bufers")
            })
            .await;
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let mut glyphs = GlyphMap::new();
        let text =
            Text::new(&mut render_context, "Good Morning", 20.0, 200, &FONT_FACE, &mut glyphs);

        let expected_origin = Point::new(0.0, 3.4487228);
        let expected_size = Vector2D::new(100.486115, 14.787117);
        assert!(
            text.bounding_box.origin.approx_eq(&expected_origin),
            "Expected bounding box origin to be close to {} but found {}",
            expected_origin,
            text.bounding_box.origin
        );
        assert!(
            text.bounding_box.size.to_vector().approx_eq(&expected_size),
            "Expected bounding box origin to be close to {} but found {}",
            expected_size,
            text.bounding_box.size
        );
    }
}
