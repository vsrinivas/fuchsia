// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cmp::Ordering,
    collections::{BTreeMap, VecDeque},
    fmt, mem,
    slice::ChunksExactMut,
};

use crate::{
    rasterizer::{search_last_by_key, CompactSegment},
    PIXEL_WIDTH, TILE_SIZE,
};

mod buffer_layout;

use buffer_layout::TileSlice;
pub use buffer_layout::{BufferLayout, BufferLayoutBuilder, Flusher};

const LAST_BYTE_MASK: i32 = 0b1111_1111;
const LAST_BIT_MASK: i32 = 0b1;

#[derive(Clone, Copy, Debug)]
pub enum FillRule {
    NonZero,
    EvenOdd,
}

impl Default for FillRule {
    fn default() -> Self {
        Self::NonZero
    }
}

#[derive(Clone, Copy, Debug)]
pub enum Fill {
    Solid([f32; 4]),
}

impl Default for Fill {
    fn default() -> Self {
        Self::Solid([0.0, 0.0, 0.0, 1.0])
    }
}

#[derive(Clone, Copy, Debug)]
pub enum BlendMode {
    Over,
}

impl Default for BlendMode {
    fn default() -> Self {
        Self::Over
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Style {
    pub fill_rule: FillRule,
    pub fill: Fill,
    pub blend_mode: BlendMode,
}

#[derive(Clone, Copy, Debug, Default)]
struct CoverCarry {
    covers: [i8; TILE_SIZE],
    layer: u16,
}

#[inline]
fn col(x: usize) -> usize {
    x * TILE_SIZE
}

#[inline]
fn entry(x: usize, y: usize) -> usize {
    x * TILE_SIZE + y
}

#[inline]
fn over(ca: f32, cb: f32, a: f32) -> f32 {
    cb.mul_add(a, ca)
}

#[inline]
fn from_area(area: i32, fill_rule: FillRule) -> f32 {
    match fill_rule {
        FillRule::NonZero => (area as f32 * 256.0f32.recip()).abs().max(0.0).min(1.0),
        FillRule::EvenOdd => {
            let number = area >> 8;
            let capped = (area & LAST_BYTE_MASK) as f32 * 256.0f32.recip();

            if number & LAST_BIT_MASK == 0 {
                capped
            } else {
                1.0 - capped
            }
        }
    }
}

#[inline]
fn linear_to_srgb_approx(l: f32) -> f32 {
    let a = 0.20101772f32;
    let b = -0.51280147f32;
    let c = 1.344401f32;
    let d = -0.030656587f32;

    let s = l.sqrt();
    let s2 = s * s;
    let s3 = s2 * s;

    let m = l * 12.92;
    let n = a.mul_add(s3, b.mul_add(s2, c.mul_add(s, d)));

    if l <= 0.0031308 {
        m
    } else {
        n
    }
}

#[inline]
fn to_byte(n: f32) -> u8 {
    n.mul_add(255.0, 0.5) as u8
}

#[inline]
fn to_bytes(color: [f32; 4]) -> [u8; 4] {
    let alpha_recip = color[3].recip();

    [
        to_byte(linear_to_srgb_approx(color[0] * alpha_recip)),
        to_byte(linear_to_srgb_approx(color[1] * alpha_recip)),
        to_byte(linear_to_srgb_approx(color[2] * alpha_recip)),
        to_byte(color[3]),
    ]
}

pub struct Painter {
    areas: [i16; TILE_SIZE * TILE_SIZE],
    covers: [i8; (TILE_SIZE + 1) * TILE_SIZE],
    colors: [[f32; 4]; TILE_SIZE * TILE_SIZE],
    queue: VecDeque<CoverCarry>,
    next_queue: VecDeque<CoverCarry>,
}

impl Painter {
    pub fn new() -> Self {
        Self {
            areas: [0; TILE_SIZE * TILE_SIZE],
            covers: [0; (TILE_SIZE + 1) * TILE_SIZE],
            colors: [[0.0, 0.0, 0.0, 1.0]; TILE_SIZE * TILE_SIZE],
            queue: VecDeque::with_capacity(8),
            next_queue: VecDeque::with_capacity(8),
        }
    }

    fn reset(&mut self) {
        self.queue.clear();
        self.next_queue.clear();
    }

    fn clear(&mut self, color: [f32; 4]) {
        self.colors.iter_mut().for_each(|pixel_color| *pixel_color = color);
    }

    fn clear_cells(&mut self) {
        self.areas.iter_mut().for_each(|area| *area = 0);
        self.covers.iter_mut().for_each(|cover| *cover = 0);
    }

    fn pop_and_use_cover(&mut self) -> Option<u16> {
        self.queue.pop_front().map(|cover_carry| {
            for (i, &cover) in cover_carry.covers.iter().enumerate() {
                self.covers[i] += cover;
            }

            cover_carry.layer
        })
    }

    #[inline]
    fn fill_at(_x: usize, _y: usize, style: Style) -> [f32; 4] {
        match style.fill {
            Fill::Solid(color) => color,
        }
    }

    fn add_cover_column(&self, x: usize, covers: &mut [i8; TILE_SIZE]) {
        let column = &self.covers[col(x)..col(x + 1)];
        for y in 0..TILE_SIZE {
            covers[y] += column[y];
        }
    }

    fn compute_areas(&self, x: usize, covers: &[i8; TILE_SIZE], areas: &mut [i32; TILE_SIZE]) {
        let column = &self.areas[col(x)..col(x + 1)];
        for y in 0..TILE_SIZE {
            areas[y] = PIXEL_WIDTH as i32 * i32::from(covers[y]) + i32::from(column[y]);
        }
    }

    fn compute_coverages(
        areas: &[i32; TILE_SIZE],
        coverages: &mut [f32; TILE_SIZE],
        fill_rule: FillRule,
    ) {
        for y in 0..TILE_SIZE {
            coverages[y] = from_area(areas[y], fill_rule);
        }
    }

    fn compute_alphas(
        x: usize,
        style: Style,
        coverages: &[f32; TILE_SIZE],
        alphas: &mut [f32; TILE_SIZE],
    ) {
        for y in 0..TILE_SIZE {
            alphas[y] = Self::fill_at(x, y, style)[3] * coverages[y];
        }
    }

    fn paint_layer(&mut self, style: Style) -> [i8; TILE_SIZE] {
        let mut covers = [0; TILE_SIZE];
        let mut areas = [0; TILE_SIZE];
        let mut coverages = [0.0; TILE_SIZE];
        let mut alphas = [0.0; TILE_SIZE];

        for x in 0..=TILE_SIZE {
            if x != 0 {
                self.compute_areas(x - 1, &covers, &mut areas);
                Self::compute_coverages(&areas, &mut coverages, style.fill_rule);
                Self::compute_alphas(x - 1, style, &coverages, &mut alphas);

                let column = &mut self.colors[col(x - 1)..col(x)];
                for y in 0..TILE_SIZE {
                    let mut new_color = Self::fill_at(x - 1, y, style);
                    let inv_alpha = 1.0 - alphas[y];

                    new_color[0] *= alphas[y];
                    new_color[1] *= alphas[y];
                    new_color[2] *= alphas[y];
                    new_color[3] = alphas[y];

                    column[y] = [
                        over(new_color[0], column[y][0], inv_alpha),
                        over(new_color[1], column[y][1], inv_alpha),
                        over(new_color[2], column[y][2], inv_alpha),
                        over(new_color[3], column[y][3], inv_alpha),
                    ];
                }
            }

            self.add_cover_column(x, &mut covers);
        }

        covers
    }

    fn process_layer_segments(&mut self, segments: &[CompactSegment], layer: u16) -> usize {
        let mut i = 0;

        segments.iter().copied().take_while(|segment| segment.layer() == layer).for_each(
            |segment| {
                i += 1;

                let x = segment.tile_x() as usize;
                let y = segment.tile_y() as usize;

                self.areas[entry(x, y)] += segment.area();
                self.covers[entry(x + 1, y)] += segment.cover();
            },
        );

        i
    }

    pub fn paint_tile<F>(&mut self, segments: &[CompactSegment], styles: &F)
    where
        F: Fn(u16) -> Style + Send + Sync,
    {
        fn next_layer(
            queue: &VecDeque<CoverCarry>,
            segment: Option<&CompactSegment>,
        ) -> Option<Ordering> {
            match (
                queue.front().map(|cover_carry| cover_carry.layer),
                segment.map(|segment| segment.layer()),
            ) {
                (Some(layer_cover), Some(layer_segment)) => Some(layer_cover.cmp(&layer_segment)),
                (Some(_), None) => Some(Ordering::Less),
                (None, Some(_)) => Some(Ordering::Greater),
                (None, None) => None,
            }
        }

        let mut i = 0;

        self.next_queue.clear();

        while let Some(ordering) = next_layer(&self.queue, segments.get(i)) {
            self.clear_cells();

            let layer = if ordering == Ordering::Less || ordering == Ordering::Equal {
                self.pop_and_use_cover().unwrap()
            } else {
                segments[i].layer()
            };

            if ordering != Ordering::Less {
                i += self.process_layer_segments(&segments[i..], layer);
            }

            let covers = self.paint_layer(styles(layer));
            if covers.iter().any(|&cover| cover != 0) {
                self.next_queue.push_back(CoverCarry { covers, layer });
            }
        }
    }

    fn covers_left(&mut self, segments: &[CompactSegment]) -> Option<usize> {
        search_last_by_key(segments, false, |segment| segment.tile_i().is_negative())
            .map(|i| {
                let i = i + 1;
                let mut left: BTreeMap<u16, [i8; TILE_SIZE]> = BTreeMap::new();

                for segment in &segments[i..] {
                    left.entry(segment.layer()).or_insert_with(|| [0; TILE_SIZE])
                        [segment.tile_y() as usize] += segment.cover();
                }

                for (layer, covers) in left {
                    self.queue.push_back(CoverCarry { layer, covers });
                }

                i
            })
            .ok()
    }

    pub fn paint_tile_row<F>(
        &mut self,
        mut segments: &[CompactSegment],
        styles: F,
        clear_color: [f32; 4],
        flusher: Option<&Box<dyn Flusher>>,
        row: ChunksExactMut<'_, TileSlice>,
    ) where
        F: Fn(u16) -> Style + Send + Sync,
    {
        if let Some(end) = self.covers_left(segments) {
            segments = &segments[..end];
        }

        for (i, tile) in row.enumerate() {
            let current_segments =
                search_last_by_key(segments, i as i16, |segment| segment.tile_i())
                    .map(|last_index| {
                        let current_segments = &segments[..=last_index];
                        segments = &segments[last_index + 1..];
                        current_segments
                    })
                    .unwrap_or(&[]);

            if !current_segments.is_empty() || !self.queue.is_empty() {
                self.clear(clear_color);

                self.paint_tile(current_segments, &styles);
                mem::swap(&mut self.queue, &mut self.next_queue);

                let len = tile.len();
                for (y, slice) in tile.iter_mut().enumerate().take(len) {
                    let slice = slice.as_mut_slice();
                    for (x, color) in slice.iter_mut().enumerate() {
                        *color = to_bytes(self.colors[entry(x, y)]);
                    }
                }

                if let Some(flusher) = flusher {
                    for slice in tile.iter_mut().take(len) {
                        let slice = slice.as_mut_slice();
                        flusher.flush(if let Some(subslice) = slice.get_mut(..TILE_SIZE) {
                            subslice
                        } else {
                            slice
                        });
                    }
                }
            }
        }
    }
}

impl fmt::Debug for Painter {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Painter")
            .field("areas", &self.areas.iter())
            .field("covers", &self.covers.iter())
            .field("color", &self.colors.iter())
            .field("stack", &self.queue.iter())
            .finish()
    }
}

impl Default for Painter {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::collections::HashMap;

    use crate::{point::Point, rasterizer::Rasterizer, LinesBuilder, Segment, TILE_SIZE};

    const BLACK: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
    const RED: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
    const RED_50: [f32; 4] = [0.5, 0.0, 0.0, 1.0];
    const GREEN: [f32; 4] = [0.0, 1.0, 0.0, 1.0];
    const GREEN_50: [f32; 4] = [0.0, 0.5, 0.0, 1.0];
    const RED_GREEN_50: [f32; 4] = [0.5, 0.5, 0.0, 1.0];

    fn line_segments(points: &[(Point<f32>, Point<f32>)]) -> Vec<CompactSegment> {
        let mut builder = LinesBuilder::new();

        for (layer, &(p0, p1)) in points.iter().enumerate() {
            builder.push(layer as u16, &Segment::new(p0, p1));
        }

        let lines = builder.build(|_| None);

        let mut rasterizer = Rasterizer::new();
        rasterizer.rasterize(&lines);

        let mut segments: Vec<_> = rasterizer.segments().iter().copied().collect();
        segments.sort_unstable();
        segments
    }

    #[test]
    fn carry_cover() {
        let mut cover_carry = CoverCarry::default();
        cover_carry.covers[1] = 16;
        cover_carry.layer = 1;

        let segments = line_segments(&[(Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32))]);

        let mut styles = HashMap::new();

        styles.insert(
            0,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(GREEN),
                blend_mode: BlendMode::Over,
            },
        );
        styles.insert(
            1,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(RED),
                blend_mode: BlendMode::Over,
            },
        );

        let mut painter = Painter::new();
        painter.queue.push_back(cover_carry);

        painter.paint_tile(&segments, &|order| styles[&order]);

        assert_eq!(painter.colors[0..2], [GREEN, RED]);
    }

    #[test]
    fn overlapping_triangles() {
        let segments = line_segments(&[
            (Point::new(0.0, 0.0), Point::new(TILE_SIZE as f32, TILE_SIZE as f32)),
            (Point::new(TILE_SIZE as f32, 0.0), Point::new(0.0, TILE_SIZE as f32)),
        ]);

        let mut styles = HashMap::new();

        styles.insert(
            0,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(GREEN),
                blend_mode: BlendMode::Over,
            },
        );
        styles.insert(
            1,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(RED),
                blend_mode: BlendMode::Over,
            },
        );

        let mut painter = Painter::new();
        painter.paint_tile(&segments, &|order| styles[&order]);

        let row_start = TILE_SIZE / 2 - 2;
        let row_end = TILE_SIZE / 2 + 2;

        let mut column = (TILE_SIZE / 2 - 2) * TILE_SIZE;
        assert_eq!(
            painter.colors[column + row_start..column + row_end],
            [GREEN_50, BLACK, BLACK, RED_50]
        );

        column += TILE_SIZE;
        assert_eq!(
            painter.colors[column + row_start..column + row_end],
            [GREEN, GREEN_50, RED_50, RED]
        );

        column += TILE_SIZE;
        assert_eq!(
            painter.colors[column + row_start..column + row_end],
            [GREEN, RED_GREEN_50, RED, RED]
        );

        column += TILE_SIZE;
        assert_eq!(
            painter.colors[column + row_start..column + row_end],
            [RED_GREEN_50, RED, RED, RED]
        );
    }
}
