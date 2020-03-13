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
    Solid([u8; 4]),
}

impl Default for Fill {
    fn default() -> Self {
        Self::Solid([0x00, 0x00, 0x00, 0xFF])
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

fn col(x: usize) -> usize {
    x * TILE_SIZE
}

fn entry(x: usize, y: usize) -> usize {
    x * TILE_SIZE + y
}

fn mul(a: u8, b: u8) -> u8 {
    let product = u16::from(a) * u16::from(b);
    // Bitwise approximation of division by 255.
    ((product + 128 + (product >> 8)) >> 8) as u8
}

fn linear(a: u8, x: u8, b: u8, y: u8) -> u8 {
    mul(a, x).saturating_add(mul(b, y))
}

fn from_area(mut area: i32, fill_rule: FillRule) -> u8 {
    match fill_rule {
        FillRule::NonZero => {
            if area < 0 {
                area = -area;
            }

            if area >= 256 {
                u8::max_value()
            } else {
                area as u8
            }
        }
        FillRule::EvenOdd => {
            let number = area >> 8;

            if area < 0 && area & LAST_BYTE_MASK != 0 {
                area -= 1;
            }

            let capped = area & LAST_BYTE_MASK;

            if number & LAST_BIT_MASK == 0 {
                capped as u8
            } else {
                u8::max_value() - capped as u8
            }
        }
    }
}

pub struct Painter {
    areas: [i16; TILE_SIZE * TILE_SIZE],
    covers: [i8; (TILE_SIZE + 1) * TILE_SIZE],
    colors: [[u8; 4]; TILE_SIZE * TILE_SIZE],
    queue: VecDeque<CoverCarry>,
    next_queue: VecDeque<CoverCarry>,
}

impl Painter {
    pub fn new() -> Self {
        Self {
            areas: [0; TILE_SIZE * TILE_SIZE],
            covers: [0; (TILE_SIZE + 1) * TILE_SIZE],
            colors: [[0x00, 0x00, 0x00, 0xFF]; TILE_SIZE * TILE_SIZE],
            queue: VecDeque::with_capacity(8),
            next_queue: VecDeque::with_capacity(8),
        }
    }

    fn reset(&mut self) {
        self.queue.clear();
        self.next_queue.clear();
    }

    fn clear(&mut self, color: [u8; 4]) {
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

    fn fill_at(_x: usize, _y: usize, style: Style) -> [u8; 4] {
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
        coverages: &mut [u8; TILE_SIZE],
        fill_rule: FillRule,
    ) {
        for y in 0..TILE_SIZE {
            coverages[y] = from_area(areas[y], fill_rule);
        }
    }

    fn compute_alphas(
        x: usize,
        style: Style,
        coverages: &[u8; TILE_SIZE],
        current_alphas: &mut [u8; TILE_SIZE],
        new_alphas: &mut [u8; TILE_SIZE],
    ) {
        for y in 0..TILE_SIZE {
            new_alphas[y] = mul(Self::fill_at(x, y, style)[3], coverages[y]);
            current_alphas[y] = 255u8 - new_alphas[y];
        }
    }

    fn paint_layer(&mut self, style: Style) -> [i8; TILE_SIZE] {
        let mut covers = [0; TILE_SIZE];
        let mut areas = [0; TILE_SIZE];
        let mut coverages = [0; TILE_SIZE];
        let mut current_alphas = [0; TILE_SIZE];
        let mut new_alphas = [0; TILE_SIZE];

        for x in 0..=TILE_SIZE {
            if x != 0 {
                self.compute_areas(x - 1, &covers, &mut areas);
                Self::compute_coverages(&areas, &mut coverages, style.fill_rule);
                Self::compute_alphas(
                    x - 1,
                    style,
                    &coverages,
                    &mut current_alphas,
                    &mut new_alphas,
                );

                let column = &mut self.colors[col(x - 1)..col(x)];
                for y in 0..TILE_SIZE {
                    let new_color = Self::fill_at(x - 1, y, style);
                    column[y] = [
                        linear(current_alphas[y], column[y][0], new_alphas[y], new_color[0]),
                        linear(current_alphas[y], column[y][1], new_alphas[y], new_color[1]),
                        linear(current_alphas[y], column[y][2], new_alphas[y], new_color[2]),
                        linear(current_alphas[y], column[y][3], new_alphas[y], new_color[3]),
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
        clear_color: [u8; 4],
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
                        *color = self.colors[entry(x, y)];
                    }
                }

                if let Some(flusher) = flusher {
                    for slice in tile.iter_mut().take(len) {
                        flusher.flush(&mut slice.as_mut_slice()[0..TILE_SIZE]);
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

    const BLACK: [u8; 4] = [0x00, 0x00, 0x00, 0xFF];
    const RED: [u8; 4] = [0xFF, 0x00, 0x00, 0xFF];
    const RED_50: [u8; 4] = [0x80, 0x00, 0x00, 0xFF];
    const GREEN: [u8; 4] = [0x00, 0xFF, 0x00, 0xFF];
    const GREEN_50: [u8; 4] = [0x00, 0x80, 0x00, 0xFF];
    const RED_GREEN_50: [u8; 4] = [0x80, 0x7F, 0x00, 0xFF];

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
