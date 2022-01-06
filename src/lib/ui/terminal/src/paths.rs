// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::{
        render::{Context as RenderContext, Path},
        Size,
    },
    euclid::{point2, vec2},
    term_model::ansi::CursorStyle,
};

// Thickness of lines is determined by multiplying thickness factor
// with the cell height. 1/16 has been chosen as that results in 1px
// thick lines for a 16px cell height.
const LINE_THICKNESS_FACTOR: f32 = 1.0 / 16.0;

fn path_for_block(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(point2(0.0, 0.0))
        .line_to(point2(size.width, 0.0))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, 0.0));
    path_builder.build()
}

pub fn path_for_underline(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let top = size.height - size.height * LINE_THICKNESS_FACTOR;
    path_builder
        .move_to(point2(0.0, top))
        .line_to(point2(size.width, top))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, top));
    path_builder.build()
}

fn path_for_beam(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let right = size.height * LINE_THICKNESS_FACTOR;
    path_builder
        .move_to(point2(0.0, 0.0))
        .line_to(point2(right, 0.0))
        .line_to(point2(right, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, 0.0));
    path_builder.build()
}

fn path_for_hollow_block(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let inset = size.height * LINE_THICKNESS_FACTOR;
    let bottom_start = size.height - inset;
    let right_start = size.width - inset;
    path_builder
        // top
        .move_to(point2(0.0, 0.0))
        .line_to(point2(size.width, 0.0))
        .line_to(point2(size.width, inset))
        .line_to(point2(0.0, inset))
        .line_to(point2(0.0, 0.0))
        // bottom
        .move_to(point2(0.0, bottom_start))
        .line_to(point2(size.width, bottom_start))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, bottom_start))
        // left
        .move_to(point2(0.0, inset))
        .line_to(point2(inset, inset))
        .line_to(point2(inset, bottom_start))
        .line_to(point2(0.0, bottom_start))
        .line_to(point2(0.0, inset))
        // right
        .move_to(point2(right_start, inset))
        .line_to(point2(size.width, inset))
        .line_to(point2(size.width, bottom_start))
        .line_to(point2(right_start, bottom_start))
        .line_to(point2(right_start, inset));
    path_builder.build()
}

pub fn path_for_strikeout(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let top = size.height / 2.0;
    let bottom = top + thickness;
    path_builder
        .move_to(point2(0.0, top))
        .line_to(point2(size.width, top))
        .line_to(point2(size.width, bottom))
        .line_to(point2(0.0, bottom))
        .line_to(point2(0.0, top));
    path_builder.build()
}

// Box Drawings Light Horizontal, "─".
fn path_for_unicode_2500(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let top = size.height / 2.0 - thickness / 2.0;
    let bottom = top + thickness;
    path_builder
        .move_to(point2(0.0, top))
        .line_to(point2(size.width, top))
        .line_to(point2(size.width, bottom))
        .line_to(point2(0.0, bottom))
        .line_to(point2(0.0, top));
    path_builder.build()
}

// Box Drawings Light Vertical, "│".
fn path_for_unicode_2502(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let left = size.width / 2.0 - thickness / 2.0;
    let right = left + thickness;
    path_builder
        .move_to(point2(left, 0.0))
        .line_to(point2(right, 0.0))
        .line_to(point2(right, size.height))
        .line_to(point2(left, size.height))
        .line_to(point2(left, 0.0));
    path_builder.build()
}

// Box Drawings Light Arc Down and Right, "╭".
fn path_for_unicode_256d(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let bottom_left = size.width / 2.0 - thickness / 2.0;
    let bottom_right = bottom_left + thickness;
    let right_top = size.height / 2.0 - thickness / 2.0;
    let right_bottom = right_top + thickness;
    let radius = (size.height * 0.25).min(size.width * 0.25);
    let inner_radius = radius - thickness / 2.0;
    let outer_radius = inner_radius + thickness;
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let outer_control_dist = kappa * outer_radius;
    let inner_control_dist = kappa * inner_radius;
    let center = point2(size.width / 2.0, size.height / 2.0) + vec2(radius, radius);
    let inner_p1 = center + vec2(-inner_control_dist, -inner_radius);
    let inner_p2 = center + vec2(-inner_radius, -inner_control_dist);
    let outer_p1 = center + vec2(-outer_radius, -outer_control_dist);
    let outer_p2 = center + vec2(-outer_control_dist, -outer_radius);
    path_builder
        .move_to(point2(size.width, right_top))
        .line_to(point2(size.width, right_bottom))
        .line_to(point2(center.x, right_bottom))
        .cubic_to(inner_p1, inner_p2, point2(bottom_right, center.y))
        .line_to(point2(bottom_right, size.height))
        .line_to(point2(bottom_left, size.height))
        .line_to(point2(bottom_left, center.y))
        .cubic_to(outer_p1, outer_p2, point2(center.x, right_top))
        .line_to(point2(size.width, right_top));
    path_builder.build()
}

// Box Drawings Light Arc Down and Left, "╮".
fn path_for_unicode_256e(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let bottom_left = size.width / 2.0 - thickness / 2.0;
    let bottom_right = bottom_left + thickness;
    let left_top = size.height / 2.0 - thickness / 2.0;
    let left_bottom = left_top + thickness;
    let radius = (size.height * 0.25).min(size.width * 0.25);
    let inner_radius = radius - thickness / 2.0;
    let outer_radius = inner_radius + thickness;
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let outer_control_dist = kappa * outer_radius;
    let inner_control_dist = kappa * inner_radius;
    let center = point2(size.width / 2.0, size.height / 2.0) + vec2(-radius, radius);
    let inner_p1 = center + vec2(inner_radius, -inner_control_dist);
    let inner_p2 = center + vec2(inner_control_dist, -inner_radius);
    let outer_p1 = center + vec2(outer_control_dist, -outer_radius);
    let outer_p2 = center + vec2(outer_radius, -outer_control_dist);
    path_builder
        .move_to(point2(0.0, left_top))
        .line_to(point2(center.x, left_top))
        .cubic_to(outer_p1, outer_p2, point2(bottom_right, center.y))
        .line_to(point2(bottom_right, size.height))
        .line_to(point2(bottom_left, size.height))
        .line_to(point2(bottom_left, center.y))
        .cubic_to(inner_p1, inner_p2, point2(center.x, left_bottom))
        .line_to(point2(0.0, left_bottom))
        .line_to(point2(0.0, left_top));
    path_builder.build()
}

// Box Drawings Light Arc Up and Left, "╯".
fn path_for_unicode_256f(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let top_left = size.width / 2.0 - thickness / 2.0;
    let top_right = top_left + thickness;
    let left_top = size.height / 2.0 - thickness / 2.0;
    let left_bottom = left_top + thickness;
    let radius = (size.height * 0.25).min(size.width * 0.25);
    let inner_radius = radius - thickness / 2.0;
    let outer_radius = inner_radius + thickness;
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let outer_control_dist = kappa * outer_radius;
    let inner_control_dist = kappa * inner_radius;
    let center = point2(size.width / 2.0, size.height / 2.0) + vec2(-radius, -radius);
    let inner_p1 = center + vec2(inner_control_dist, inner_radius);
    let inner_p2 = center + vec2(inner_radius, inner_control_dist);
    let outer_p1 = center + vec2(outer_radius, outer_control_dist);
    let outer_p2 = center + vec2(outer_control_dist, outer_radius);
    path_builder
        .move_to(point2(top_left, 0.0))
        .line_to(point2(top_right, 0.0))
        .line_to(point2(top_right, center.y))
        .cubic_to(outer_p1, outer_p2, point2(center.x, left_bottom))
        .line_to(point2(0.0, left_bottom))
        .line_to(point2(0.0, left_top))
        .line_to(point2(center.x, left_top))
        .cubic_to(inner_p1, inner_p2, point2(top_left, center.y))
        .line_to(point2(top_left, 0.0));
    path_builder.build()
}

// Box Drawings Light Arc Up and Right, "╰".
fn path_for_unicode_2570(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let top_left = size.width / 2.0 - thickness / 2.0;
    let top_right = top_left + thickness;
    let right_top = size.height / 2.0 - thickness / 2.0;
    let right_bottom = right_top + thickness;
    let radius = (size.height * 0.25).min(size.width * 0.25);
    let inner_radius = radius - thickness / 2.0;
    let outer_radius = inner_radius + thickness;
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let outer_control_dist = kappa * outer_radius;
    let inner_control_dist = kappa * inner_radius;
    let center = point2(size.width / 2.0, size.height / 2.0) + vec2(radius, -radius);
    let inner_p1 = center + vec2(-inner_radius, inner_control_dist);
    let inner_p2 = center + vec2(-inner_control_dist, inner_radius);
    let outer_p1 = center + vec2(-outer_control_dist, outer_radius);
    let outer_p2 = center + vec2(-outer_radius, outer_control_dist);
    path_builder
        .move_to(point2(top_left, 0.0))
        .line_to(point2(top_right, 0.0))
        .line_to(point2(top_right, center.y))
        .cubic_to(inner_p1, inner_p2, point2(center.x, right_top))
        .line_to(point2(size.width, right_top))
        .line_to(point2(size.width, right_bottom))
        .line_to(point2(center.x, right_bottom))
        .cubic_to(outer_p1, outer_p2, point2(top_left, center.y))
        .line_to(point2(top_left, 0.0));
    path_builder.build()
}

// Light Shade, "░".
fn path_for_unicode_2591(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const GRID_SIZE: usize = 8;
    let scale_x = size.width / GRID_SIZE as f32;
    let scale_y = size.height / GRID_SIZE as f32;
    for y in 0..GRID_SIZE {
        for x in 0..GRID_SIZE {
            let offset = if y % 2 == 0 { x } else { x + 2 };
            // Fill every fourth grid cell.
            if offset % 4 == 0 {
                let x0 = x as f32 * scale_x;
                let y0 = y as f32 * scale_y;
                let x1 = (x + 1) as f32 * scale_x;
                let y1 = (y + 1) as f32 * scale_y;
                path_builder
                    .move_to(point2(x0, y0))
                    .line_to(point2(x1, y0))
                    .line_to(point2(x1, y1))
                    .line_to(point2(x0, y1))
                    .line_to(point2(x0, y0));
            }
        }
    }
    path_builder.build()
}

// Medium Shade, "▒".
fn path_for_unicode_2592(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const GRID_SIZE: usize = 9;
    let scale_x = size.width / GRID_SIZE as f32;
    let scale_y = size.height / GRID_SIZE as f32;
    for y in 0..GRID_SIZE {
        for x in 0..GRID_SIZE {
            let offset = if y % 2 == 0 { x } else { x + 1 };
            // Fill every other grid cell.
            if offset % 2 == 0 {
                let x0 = x as f32 * scale_x;
                let y0 = y as f32 * scale_y;
                let x1 = (x + 1) as f32 * scale_x;
                let y1 = (y + 1) as f32 * scale_y;
                path_builder
                    .move_to(point2(x0, y0))
                    .line_to(point2(x1, y0))
                    .line_to(point2(x1, y1))
                    .line_to(point2(x0, y1))
                    .line_to(point2(x0, y0));
            }
        }
    }
    path_builder.build()
}

// Dark Shade, "▓".
fn path_for_unicode_2593(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const GRID_SIZE: usize = 8;
    let scale_x = size.width / GRID_SIZE as f32;
    let scale_y = size.height / GRID_SIZE as f32;
    for y in 0..GRID_SIZE {
        for x in 0..GRID_SIZE {
            let offset = if y % 2 == 0 { x } else { x + 2 };
            // Skip every fourth grid cell.
            if offset % 4 != 0 {
                let x0 = x as f32 * scale_x;
                let y0 = y as f32 * scale_y;
                let x1 = (x + 1) as f32 * scale_x;
                let y1 = (y + 1) as f32 * scale_y;
                path_builder
                    .move_to(point2(x0, y0))
                    .line_to(point2(x1, y0))
                    .line_to(point2(x1, y1))
                    .line_to(point2(x0, y1))
                    .line_to(point2(x0, y0));
            }
        }
    }
    path_builder.build()
}

// Heavy Check Mark, "✔".
fn path_for_unicode_2714(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const OFFSET_FACTOR: f32 = 1.0 / 12.0;
    let offset = size.height * OFFSET_FACTOR;
    let center_offset = offset * 3.0;
    let left_top = 0.4 * size.height;
    let right_top = 0.25 * size.height;
    let center = 0.3 * size.width;
    let bottom = 0.75 * size.height;
    let left = 0.05;
    let right = size.width - 0.05;
    path_builder
        .move_to(point2(left, left_top + offset))
        .line_to(point2(left + offset, left_top))
        .line_to(point2(center, bottom - center_offset))
        .line_to(point2(right - offset, right_top))
        .line_to(point2(right, right_top + offset))
        .line_to(point2(center, bottom))
        .line_to(point2(left, left_top + offset));
    path_builder.build()
}

// Heavy Ballot X, "✘".
fn path_for_unicode_2718(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const OFFSET_FACTOR: f32 = 1.0 / 12.0;
    let offset = size.height * OFFSET_FACTOR;
    let center_offset = offset;
    let top = 0.25 * size.height;
    let center_x = size.width / 2.0;
    let center_y = size.height / 2.0;
    let bottom = 0.75 * size.height;
    let left = 0.05;
    let right = size.width - 0.05;
    path_builder
        .move_to(point2(left, top + offset))
        .line_to(point2(left + offset, top))
        .line_to(point2(center_x, center_y - center_offset))
        .line_to(point2(right - offset, top))
        .line_to(point2(right, top + offset))
        .line_to(point2(center_x + center_offset, center_y))
        .line_to(point2(right, bottom - offset))
        .line_to(point2(right - offset, bottom))
        .line_to(point2(center_x, center_y + center_offset))
        .line_to(point2(left + offset, bottom))
        .line_to(point2(left, bottom - offset))
        .line_to(point2(center_x - center_offset, center_y))
        .line_to(point2(left, top + offset));
    path_builder.build()
}

// Heavy Left-Pointing Angle Quotation Mark Ornament, "❮".
fn path_for_unicode_276e(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const THICKNESS_FACTOR: f32 = 1.0 / 8.0;
    let thickness = size.height * THICKNESS_FACTOR;
    let top = 0.25 * size.height;
    let bottom = 0.85 * size.height;
    let left = 0.2 * size.width;
    let right = 0.8 * size.width;
    let center_y = top + (bottom - top) / 2.0;
    path_builder
        .move_to(point2(right - thickness, top))
        .line_to(point2(right, top))
        .line_to(point2(left + thickness, center_y))
        .line_to(point2(right, bottom))
        .line_to(point2(right - thickness, bottom))
        .line_to(point2(left, center_y))
        .line_to(point2(right - thickness, top));
    path_builder.build()
}

// Heavy Right-Pointing Angle Quotation Mark Ornament, "❯".
fn path_for_unicode_276f(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    const THICKNESS_FACTOR: f32 = 1.0 / 8.0;
    let thickness = size.height * THICKNESS_FACTOR;
    let top = 0.25 * size.height;
    let bottom = 0.85 * size.height;
    let left = 0.2 * size.width;
    let right = 0.8 * size.width;
    let center_y = top + (bottom - top) / 2.0;
    path_builder
        .move_to(point2(left, top))
        .line_to(point2(left + thickness, top))
        .line_to(point2(right, center_y))
        .line_to(point2(left + thickness, bottom))
        .line_to(point2(left, bottom))
        .line_to(point2(right - thickness, center_y))
        .line_to(point2(left, top));
    path_builder.build()
}

// Triangle right, "".
fn path_for_unicode_e0b0(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(point2(0.0, 0.0))
        .line_to(point2(size.width, size.height / 2.0))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, 0.0));
    path_builder.build()
}

// Angle right, "".
fn path_for_unicode_e0b1(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    // Requires 45 degree angle for correct thickness.
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let offset = (2.0 * thickness * thickness).sqrt() / 2.0;
    path_builder
        .move_to(point2(offset, 0.0))
        .line_to(point2(size.width + offset, size.height / 2.0))
        .line_to(point2(offset, size.height))
        .line_to(point2(-offset, size.height))
        .line_to(point2(size.width - offset, size.height / 2.0))
        .line_to(point2(-offset, 0.0))
        .line_to(point2(offset, 0.0));
    path_builder.build()
}

// Triangle left, "".
fn path_for_unicode_e0b2(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(point2(size.width, 0.0))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height / 2.0))
        .line_to(point2(size.width, 0.0));
    path_builder.build()
}

// Angle left, "".
fn path_for_unicode_e0b3(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    // Requires 45 degree angle for correct thickness.
    let thickness = size.height * LINE_THICKNESS_FACTOR;
    let offset = (2.0 * thickness * thickness).sqrt() / 2.0;
    path_builder
        .move_to(point2(size.width + offset, 0.0))
        .line_to(point2(offset, size.height / 2.0))
        .line_to(point2(size.width + offset, size.height))
        .line_to(point2(size.width - offset, size.height))
        .line_to(point2(-offset, size.height / 2.0))
        .line_to(point2(size.width - offset, 0.0))
        .line_to(point2(size.width + offset, 0.0));
    path_builder.build()
}

// Lower right triangle, "".
fn path_for_unicode_e0ba(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(point2(size.width, 0.0))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(size.width, 0.0));
    path_builder.build()
}

// Upper left triangle, "".
fn path_for_unicode_e0bc(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(point2(0.0, 0.0))
        .line_to(point2(size.width, 0.0))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, 0.0));
    path_builder.build()
}

pub fn maybe_path_for_cursor_style(
    render_context: &mut RenderContext,
    cursor_style: CursorStyle,
    cell_size: &Size,
) -> Option<Path> {
    match cursor_style {
        CursorStyle::Block => Some(path_for_block(cell_size, render_context)),
        CursorStyle::Underline => Some(path_for_underline(cell_size, render_context)),
        CursorStyle::Beam => Some(path_for_beam(cell_size, render_context)),
        CursorStyle::HollowBlock => Some(path_for_hollow_block(cell_size, render_context)),
        CursorStyle::Hidden => None,
    }
}

pub fn maybe_path_for_char(
    render_context: &mut RenderContext,
    c: char,
    cell_size: &Size,
) -> Option<Path> {
    match c {
        '\u{2500}' => Some(path_for_unicode_2500(cell_size, render_context)),
        '\u{2502}' => Some(path_for_unicode_2502(cell_size, render_context)),
        '\u{256d}' => Some(path_for_unicode_256d(cell_size, render_context)),
        '\u{256e}' => Some(path_for_unicode_256e(cell_size, render_context)),
        '\u{256f}' => Some(path_for_unicode_256f(cell_size, render_context)),
        '\u{2570}' => Some(path_for_unicode_2570(cell_size, render_context)),
        '\u{2591}' => Some(path_for_unicode_2591(cell_size, render_context)),
        '\u{2592}' => Some(path_for_unicode_2592(cell_size, render_context)),
        '\u{2593}' => Some(path_for_unicode_2593(cell_size, render_context)),
        '\u{2714}' => Some(path_for_unicode_2714(cell_size, render_context)),
        '\u{2718}' => Some(path_for_unicode_2718(cell_size, render_context)),
        '\u{276e}' => Some(path_for_unicode_276e(cell_size, render_context)),
        '\u{276f}' => Some(path_for_unicode_276f(cell_size, render_context)),
        '\u{e0b0}' => Some(path_for_unicode_e0b0(cell_size, render_context)),
        '\u{e0b1}' => Some(path_for_unicode_e0b1(cell_size, render_context)),
        '\u{e0b2}' => Some(path_for_unicode_e0b2(cell_size, render_context)),
        '\u{e0b3}' => Some(path_for_unicode_e0b3(cell_size, render_context)),
        '\u{e0ba}' => Some(path_for_unicode_e0ba(cell_size, render_context)),
        '\u{e0bc}' => Some(path_for_unicode_e0bc(cell_size, render_context)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        carnelian::{
            drawing::DisplayRotation,
            render::{generic, ContextInner},
        },
        euclid::size2,
    };

    #[test]
    fn check_cursor_paths() -> Result<(), Error> {
        const SUPPORTED_CURSOR_STYLES: &[CursorStyle] = &[
            CursorStyle::Block,
            CursorStyle::Underline,
            CursorStyle::Beam,
            CursorStyle::HollowBlock,
        ];
        let size = size2(64, 64);
        let mold_context = generic::Mold::new_context_without_token(size, DisplayRotation::Deg0);
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let cell_size = Size::new(8.0, 16.0);
        for cursor_style in SUPPORTED_CURSOR_STYLES {
            let result =
                maybe_path_for_cursor_style(&mut render_context, *cursor_style, &cell_size);
            assert_eq!(result.is_some(), true);
        }
        Ok(())
    }

    #[test]
    fn check_strikeout_path() -> Result<(), Error> {
        let size = size2(64, 64);
        let mold_context = generic::Mold::new_context_without_token(size, DisplayRotation::Deg0);
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let cell_size = Size::new(8.0, 16.0);
        let _ = path_for_strikeout(&cell_size, &mut render_context);
        Ok(())
    }

    #[test]
    fn check_unicode_paths() -> Result<(), Error> {
        const SUPPORTED_UNICODE_CHARS: &[char] = &[
            '\u{2500}', '\u{2502}', '\u{256d}', '\u{256e}', '\u{256f}', '\u{2570}', '\u{2591}',
            '\u{2592}', '\u{2593}', '\u{2714}', '\u{2718}', '\u{276e}', '\u{276f}', '\u{e0b0}',
            '\u{e0b1}', '\u{e0b2}', '\u{e0b3}', '\u{e0ba}', '\u{e0bc}',
        ];
        let size = size2(64, 64);
        let mold_context = generic::Mold::new_context_without_token(size, DisplayRotation::Deg0);
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let cell_size = Size::new(8.0, 16.0);
        for c in SUPPORTED_UNICODE_CHARS {
            let result = maybe_path_for_char(&mut render_context, *c, &cell_size);
            assert_eq!(result.is_some(), true);
        }
        Ok(())
    }
}
