// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Functions for drawing in Carnelian
//! Carnelian uses the Render abstraction over Mold and Spinel
//! to put pixels on screen. The items in this module are higher-
//! level drawing primitives.

use crate::{
    geometry::{Coord, Corners, Point, Rect},
    render::{Context as RenderContext, Path},
};
use euclid::Vector2D;

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
