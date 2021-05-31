// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cmp::Ordering,
    collections::{BTreeMap, VecDeque},
    mem,
    slice::ChunksExactMut,
};

use crate::{
    rasterizer::{search_last_by_key, CompactSegment},
    simd::{f32x8, i16x16, i32x8, i8x16, u8x32, u8x8, Simd},
    PIXEL_WIDTH, TILE_SIZE,
};

mod buffer_layout;
#[macro_use]
mod style;

use buffer_layout::TileSlice;
pub use buffer_layout::{BufferLayout, BufferLayoutBuilder, Flusher, Rect};

use dashmap::{mapref::entry::Entry, DashMap};
pub use style::{BlendMode, Fill, FillRule, Gradient, GradientBuilder, GradientType, Style};

const LAST_BYTE_MASK: i32 = 0b1111_1111;
const LAST_BIT_MASK: i32 = 0b1;

macro_rules! cols {
    ( & $array:expr, $x0:expr, $x1:expr ) => {{
        fn size_of_el<T: Simd>(_: &[T]) -> usize {
            T::LANES
        }

        let from = $x0 * crate::TILE_SIZE / size_of_el(&$array);
        let to = $x1 * crate::TILE_SIZE / size_of_el(&$array);

        &$array[from..to]
    }};

    ( & mut $array:expr, $x0:expr, $x1:expr ) => {{
        fn size_of_el<T: Simd>(_: &[T]) -> usize {
            T::LANES
        }

        let from = $x0 * crate::TILE_SIZE / size_of_el(&$array);
        let to = $x1 * crate::TILE_SIZE / size_of_el(&$array);

        &mut $array[from..to]
    }};
}

#[inline]
fn from_area(area: i32x8, fill_rule: FillRule) -> f32x8 {
    match fill_rule {
        FillRule::NonZero => {
            let area: f32x8 = area.into();
            (area * f32x8::splat(256.0f32.recip()))
                .abs()
                .clamp(f32x8::splat(0.0), f32x8::splat(1.0))
        }
        FillRule::EvenOdd => {
            let number = area >> i32x8::splat(8);
            let masked: f32x8 = (area & i32x8::splat(LAST_BYTE_MASK)).into();
            let capped = masked * f32x8::splat(256.0f32.recip());

            let mask = (number & i32x8::splat(LAST_BIT_MASK)).eq(i32x8::splat(0));
            capped.select(f32x8::splat(1.0) - capped, mask)
        }
    }
}

#[inline]
fn linear_to_srgb_approx_simd(l: f32x8) -> f32x8 {
    let a = f32x8::splat(0.20101772f32);
    let b = f32x8::splat(-0.51280147f32);
    let c = f32x8::splat(1.344401f32);
    let d = f32x8::splat(-0.030656587f32);

    let s = l.sqrt();
    let s2 = l;
    let s3 = s2 * s;

    let m = l * f32x8::splat(12.92);
    let n = a.mul_add(s3, b.mul_add(s2, c.mul_add(s, d)));

    m.select(n, l.le(f32x8::splat(0.0031308)))
}

#[inline]
fn linear_to_srgb_approx(l: f32) -> f32 {
    let a = 0.20101772f32;
    let b = -0.51280147f32;
    let c = 1.344401f32;
    let d = -0.030656587f32;

    let s = l.sqrt();
    let s2 = l;
    let s3 = s2 * s;

    if l <= 0.0031308 {
        l * 12.92
    } else {
        a.mul_add(s3, b.mul_add(s2, c.mul_add(s, d)))
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

pub trait LayerStyles: Send + Sync {
    fn get(&self, layer: u16) -> Style;
    fn is_unchanged(&self, layer: u16) -> bool;
}

impl<F> LayerStyles for F
where
    F: Fn(u16) -> Style + Send + Sync,
{
    fn get(&self, layer: u16) -> Style {
        self(layer)
    }

    fn is_unchanged(&self, _: u16) -> bool {
        false
    }
}

#[derive(Clone, Copy, Debug)]
struct CoverCarry {
    covers: [i8x16; TILE_SIZE / 16],
    layer: u16,
}

impl CoverCarry {
    pub fn is_empty(&self, fill_rule: FillRule) -> bool {
        match fill_rule {
            FillRule::NonZero => self.covers.iter().all(|&cover| cover.eq(i8x16::splat(0)).all()),
            FillRule::EvenOdd => self
                .covers
                .iter()
                .all(|&cover| (cover.abs() & i8x16::splat(31)).eq(i8x16::splat(0)).all()),
        }
    }

    pub fn is_full(&self, fill_rule: FillRule) -> bool {
        match fill_rule {
            FillRule::NonZero => self
                .covers
                .iter()
                .all(|&cover| cover.abs().eq(i8x16::splat(PIXEL_WIDTH as i8)).all()),
            FillRule::EvenOdd => self.covers.iter().any(|&cover| {
                (cover.abs() & i8x16::splat(0b1_1111)).eq(i8x16::splat(0b1_0000)).all()
            }),
        }
    }
}

#[derive(Debug)]
pub struct Painter {
    areas: [i16x16; TILE_SIZE * TILE_SIZE / i16x16::LANES],
    covers: [i8x16; (TILE_SIZE + 1) * TILE_SIZE / i8x16::LANES],
    c0: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    c1: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    c2: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    alpha: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    srgb: [u8x32; TILE_SIZE * TILE_SIZE * 4 / u8x32::LANES],
    queue: VecDeque<CoverCarry>,
    next_queue: VecDeque<CoverCarry>,
}

impl Painter {
    pub fn new() -> Self {
        Self {
            areas: [i16x16::splat(0); TILE_SIZE * TILE_SIZE / i16x16::LANES],
            covers: [i8x16::splat(0); (TILE_SIZE + 1) * TILE_SIZE / i8x16::LANES],
            c0: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            c1: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            c2: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            alpha: [f32x8::splat(1.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            srgb: [u8x32::splat(0); TILE_SIZE * TILE_SIZE * 4 / u8x32::LANES],
            queue: VecDeque::with_capacity(8),
            next_queue: VecDeque::with_capacity(8),
        }
    }

    pub fn reset(&mut self) {
        self.queue.clear();
        self.next_queue.clear();
    }

    fn clear(&mut self, color: [f32; 4]) {
        self.c0.iter_mut().for_each(|c0| *c0 = f32x8::splat(color[0]));
        self.c1.iter_mut().for_each(|c1| *c1 = f32x8::splat(color[1]));
        self.c2.iter_mut().for_each(|c2| *c2 = f32x8::splat(color[2]));
        self.alpha.iter_mut().for_each(|alpha| *alpha = f32x8::splat(color[3]));
    }

    fn clear_cells(&mut self) {
        self.areas.iter_mut().for_each(|area| *area = i16x16::splat(0));
        self.covers.iter_mut().for_each(|cover| *cover = i8x16::splat(0));
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
    fn fill_at(x: usize, y: usize, style: &Style) -> [f32x8; 4] {
        match &style.fill {
            Fill::Solid([c0, c1, c2, alpha]) => {
                [f32x8::splat(*c0), f32x8::splat(*c1), f32x8::splat(*c2), f32x8::splat(*alpha)]
            }
            Fill::Gradient(gradient) => gradient.color_at(x as f32, y as f32),
        }
    }

    fn compute_areas(
        &self,
        x: usize,
        covers: &[i8x16; TILE_SIZE / i8x16::LANES],
        areas: &mut [i32x8; TILE_SIZE / i32x8::LANES],
    ) {
        let column = cols!(&self.areas, x, x + 1);
        for y in 0..covers.len() {
            let covers: [i32x8; 2] = covers[y].into();
            let column: [i32x8; 2] = column[y].into();

            for yy in 0..2 {
                areas[2 * y + yy] = i32x8::splat(PIXEL_WIDTH as i32) * covers[yy] + column[yy];
            }
        }
    }

    fn paint_layer(
        &mut self,
        tile_i: usize,
        tile_j: usize,
        style: &Style,
    ) -> [i8x16; TILE_SIZE / i8x16::LANES] {
        let mut areas = [i32x8::splat(0); TILE_SIZE / i32x8::LANES];
        let mut covers = [i8x16::splat(0); TILE_SIZE / i8x16::LANES];
        let mut coverages = [f32x8::splat(0.0); TILE_SIZE / f32x8::LANES];
        let mut alphas = [f32x8::splat(0.0); TILE_SIZE / f32x8::LANES];
        let mut inv_alphas = [f32x8::splat(0.0); TILE_SIZE / f32x8::LANES];

        for x in 0..=TILE_SIZE {
            if x != 0 {
                self.compute_areas(x - 1, &covers, &mut areas);

                for y in 0..coverages.len() {
                    coverages[y] = from_area(areas[y], style.fill_rule);

                    if coverages[y].eq(f32x8::splat(0.0)).all() {
                        continue;
                    }

                    let fill = Self::fill_at(
                        x + tile_i * TILE_SIZE,
                        y * f32x8::LANES + tile_j * TILE_SIZE,
                        &style,
                    );

                    let c0 = cols!(&mut self.c0, x - 1, x);
                    let c1 = cols!(&mut self.c1, x - 1, x);
                    let c2 = cols!(&mut self.c2, x - 1, x);
                    let alpha = cols!(&mut self.alpha, x - 1, x);

                    alphas[y] = fill[3] * coverages[y];
                    inv_alphas[y] = f32x8::splat(1.0) - alphas[y];

                    let [mut current_c0, mut current_c1, mut current_c2] = blend_function!(
                        style.blend_mode,
                        c0[y],
                        c1[y],
                        c2[y],
                        fill[0],
                        fill[1],
                        fill[2],
                    );

                    current_c0 *= alphas[y];
                    current_c1 *= alphas[y];
                    current_c2 *= alphas[y];
                    let current_alpha = alphas[y];

                    c0[y] = c0[y].mul_add(inv_alphas[y], current_c0);
                    c1[y] = c1[y].mul_add(inv_alphas[y], current_c1);
                    c2[y] = c2[y].mul_add(inv_alphas[y], current_c2);
                    alpha[y] = alpha[y].mul_add(inv_alphas[y], current_alpha);
                }
            }

            let column = cols!(&self.covers, x, x + 1);
            for y in 0..column.len() {
                covers[y] += column[y];
            }
        }

        covers
    }

    fn for_each_layer_segments<F>(segments: &[CompactSegment], layer: u16, mut f: F) -> usize
    where
        F: FnMut(CompactSegment),
    {
        let mut i = 0;

        segments.iter().copied().take_while(|segment| segment.layer() == layer).for_each(
            |segment| {
                i += 1;

                f(segment);
            },
        );

        i
    }

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

    pub fn paint_tile<S: LayerStyles>(
        &mut self,
        tile_i: usize,
        tile_j: usize,
        segments: &[CompactSegment],
        styles: &S,
    ) {
        let mut i = 0;

        self.next_queue.clear();

        while let Some(ordering) = Self::next_layer(&self.queue, segments.get(i)) {
            self.clear_cells();

            let layer = if ordering == Ordering::Less || ordering == Ordering::Equal {
                self.pop_and_use_cover().unwrap()
            } else {
                segments[i].layer()
            };

            if ordering != Ordering::Less {
                i += Self::for_each_layer_segments(&segments[i..], layer, |segment| {
                    let x = segment.tile_x() as usize;
                    let y = segment.tile_y() as usize;

                    let areas: &mut [i16; TILE_SIZE * TILE_SIZE] =
                        unsafe { mem::transmute(&mut self.areas) };
                    let covers: &mut [i8; (TILE_SIZE + 1) * TILE_SIZE] =
                        unsafe { mem::transmute(&mut self.covers) };

                    areas[x * TILE_SIZE + y] += segment.area();
                    covers[(x + 1) * TILE_SIZE + y] += segment.cover();
                });
            }

            let style = styles.get(layer);
            let cover_carry =
                CoverCarry { covers: self.paint_layer(tile_i, tile_j, &style), layer };

            if !cover_carry.is_empty(style.fill_rule) {
                self.next_queue.push_back(cover_carry);
            }
        }
    }

    fn top_carry_layers_solid_opaque<S: LayerStyles>(
        &mut self,
        segments: &[CompactSegment],
        styles: &S,
        clear_color: [f32; 4],
    ) -> Option<[f32; 4]> {
        if self.queue.is_empty() {
            return None;
        }

        let first_incomplete_layer_index = self
            .queue
            .iter()
            .enumerate()
            .rev()
            .find(|(_, cover_carry)| !cover_carry.is_full(styles.get(cover_carry.layer).fill_rule))
            .map(|(i, _)| i);

        let mut first_incomplete_layer = None;

        if let Some(i) = first_incomplete_layer_index {
            if i == self.queue.len() - 1 {
                // Last layer is incomplete.
                return None;
            }

            first_incomplete_layer = Some(self.queue[i + 1].layer);
        }

        if let Some(segment_layer) = segments.last().map(|segment| segment.layer()) {
            if segment_layer >= first_incomplete_layer.unwrap_or_else(|| self.queue[0].layer) {
                // There are segments over the complete top carry layer.
                return None;
            }
        }

        let bottom_color = match first_incomplete_layer.map(|layer| styles.get(layer)) {
            Some(Style { fill: Fill::Solid(color), blend_mode: BlendMode::Over, .. })
                if color[3] == 1.0 =>
            {
                Some(color)
            }
            None => Some(clear_color),
            // Bottom layer is not opaque.
            _ => None,
        };

        bottom_color.and_then(|bottom_color| {
            let color = self.queue.iter().try_fold(bottom_color, |dst, cover_carry| {
                match styles.get(cover_carry.layer) {
                    Style { fill: Fill::Solid(color), blend_mode, .. } => {
                        Some(blend_mode.blend(dst, color))
                    }
                    // Fill is not solid.
                    _ => None,
                }
            })?;

            let mut i = 0;

            self.next_queue.clear();

            while let Some(ordering) = Self::next_layer(&self.queue, segments.get(i)) {
                match ordering {
                    Ordering::Less => self.next_queue.push_back(self.queue.pop_front().unwrap()),
                    Ordering::Equal => {
                        let mut cover_carry = self.queue.pop_front().unwrap();

                        i += Self::for_each_layer_segments(
                            &segments[i..],
                            cover_carry.layer,
                            |segment| {
                                let covers: &mut [i8; TILE_SIZE] =
                                    unsafe { mem::transmute(&mut cover_carry.covers) };
                                covers[segment.tile_y() as usize] += segment.cover();
                            },
                        );

                        self.next_queue.push_back(cover_carry);
                    }
                    Ordering::Greater => {
                        let layer = segments[i].layer();
                        let mut covers = [i8x16::splat(0); TILE_SIZE / 16];

                        i += Self::for_each_layer_segments(&segments[i..], layer, |segment| {
                            let covers: &mut [i8; TILE_SIZE] =
                                unsafe { mem::transmute(&mut covers) };
                            covers[segment.tile_y() as usize] += segment.cover();
                        });

                        self.next_queue.push_back(CoverCarry { covers, layer });
                    }
                }
            }

            Some(color)
        })
    }

    fn is_unchanged<S: LayerStyles>(
        &mut self,
        mut entry: Entry<'_, (usize, usize), usize>,
        segments: &[CompactSegment],
        styles: &S,
    ) -> bool {
        let queue_layers = self.queue.len();

        fn reduce_layers_is_unchanged<S: LayerStyles>(
            segments: &[CompactSegment],
            styles: &S,
        ) -> (usize, bool) {
            let first_layer = segments.first().map(|s| s.layer());
            let last_layer = segments.last().map(|s| s.layer());

            if first_layer == last_layer {
                return (
                    if first_layer.is_some() { 1 } else { 0 },
                    first_layer.map(|l| styles.is_unchanged(l)).unwrap_or(true),
                );
            }

            let mid = segments.len() / 2;

            let left = reduce_layers_is_unchanged(&segments[..mid], styles);
            let right = reduce_layers_is_unchanged(&segments[mid..], styles);

            (left.0 + right.0, left.1 && right.1)
        }

        let (segment_layers, is_unchanged) = reduce_layers_is_unchanged(segments, styles);

        let total_layers = queue_layers + segment_layers;
        let is_unchanged = is_unchanged
            & self.queue.iter().all(|cover_carry| styles.is_unchanged(cover_carry.layer));

        match entry {
            Entry::Occupied(ref mut occupied) => {
                let old_layers = mem::replace(occupied.get_mut(), total_layers);
                old_layers == total_layers && is_unchanged
            }
            Entry::Vacant(vacant) => {
                vacant.insert(total_layers);
                false
            }
        }
    }

    fn compute_srgb(&mut self) {
        for (channel, alpha) in self.c0.iter_mut().zip(self.alpha.iter()) {
            *channel = (linear_to_srgb_approx_simd(*channel) * *alpha) * f32x8::splat(255.0);
        }
        for (channel, alpha) in self.c1.iter_mut().zip(self.alpha.iter()) {
            *channel = (linear_to_srgb_approx_simd(*channel) * *alpha) * f32x8::splat(255.0);
        }
        for (channel, alpha) in self.c2.iter_mut().zip(self.alpha.iter()) {
            *channel = (linear_to_srgb_approx_simd(*channel) * *alpha) * f32x8::splat(255.0);
        }
        for alpha in self.alpha.iter_mut() {
            *alpha = alpha.mul_add(f32x8::splat(255.0), f32x8::splat(0.5));
        }

        let srgb: &mut [u8x8; TILE_SIZE * TILE_SIZE * 4 / 8] =
            unsafe { mem::transmute(&mut self.srgb) };

        for ((((c0, c1), c2), alpha), srgb) in self
            .c0
            .iter()
            .zip(self.c1.iter())
            .zip(self.c2.iter())
            .zip(self.alpha.iter())
            .zip(srgb.chunks_mut(4))
        {
            srgb[0] = (*c0).into();
            srgb[1] = (*c1).into();
            srgb[2] = (*c2).into();
            srgb[3] = (*alpha).into();
        }

        for srgb in self.srgb.iter_mut() {
            *srgb = srgb.swizzle::<0, 8, 16, 24, 1, 9, 17, 25, 2, 10, 18, 26, 3, 11, 19, 27, 4, 12, 20, 28, 5, 13,
            21, 29, 6, 14, 22, 30, 7, 15, 23, 31>();
        }
    }

    fn write_to_tile(
        &self,
        tile: &mut [TileSlice],
        solid_color: Option<[u8; 4]>,
        flusher: Option<&dyn Flusher>,
    ) {
        let tile_len = tile.len();
        if let Some(solid_color) = solid_color {
            for slice in tile.iter_mut().take(tile_len) {
                let slice = slice.as_mut_slice();
                for color in slice.iter_mut() {
                    *color = solid_color;
                }
            }
        } else {
            let srgb: &[[u8; 4]] = unsafe {
                std::slice::from_raw_parts(mem::transmute(self.srgb.as_ptr()), self.srgb.len() * 16)
            };

            for (y, slice) in tile.iter_mut().enumerate().take(tile_len) {
                let slice = slice.as_mut_slice();
                for (x, color) in slice.iter_mut().enumerate() {
                    *color = srgb[x * TILE_SIZE + y];
                }
            }
        }

        if let Some(flusher) = flusher {
            for slice in tile.iter_mut().take(tile_len) {
                let slice = slice.as_mut_slice();
                flusher.flush(if let Some(subslice) = slice.get_mut(..TILE_SIZE) {
                    subslice
                } else {
                    slice
                });
            }
        }
    }

    pub fn paint_tile_row<S: LayerStyles>(
        &mut self,
        j: usize,
        mut segments: &[CompactSegment],
        styles: &S,
        clear_color: [f32; 4],
        layers_per_tile: Option<&DashMap<(usize, usize), usize>>,
        flusher: Option<&dyn Flusher>,
        row: ChunksExactMut<'_, TileSlice>,
        crop: Option<Rect>,
    ) {
        fn acc_covers(
            segments: &[CompactSegment],
            covers: &mut BTreeMap<u16, [i8x16; TILE_SIZE / 16]>,
        ) {
            for segment in segments {
                let covers = covers
                    .entry(segment.layer())
                    .or_insert_with(|| [i8x16::splat(0); TILE_SIZE / 16]);

                let covers: &mut [i8; TILE_SIZE] = unsafe { mem::transmute(covers) };
                covers[segment.tile_y() as usize] += segment.cover();
            }
        }

        let mut covers_left_of_row: BTreeMap<u16, [i8x16; TILE_SIZE / 16]> = BTreeMap::new();
        let mut populate_covers = |limit: Option<i16>| {
            let query = search_last_by_key(segments, false, |segment| match limit {
                Some(limit) => (segment.tile_i() - limit).is_positive(),
                None => segment.tile_i().is_negative(),
            });

            if let Ok(i) = query {
                let i = i + 1;

                match limit {
                    Some(_) => {
                        acc_covers(&segments[..i], &mut covers_left_of_row);
                        segments = &segments[i..];
                    }
                    None => {
                        acc_covers(&segments[i..], &mut covers_left_of_row);
                        segments = &segments[..i];
                    }
                }
            }
        };

        populate_covers(None);

        if let Some(rect) = &crop {
            if rect.horizontal.start > 0 {
                populate_covers(Some(rect.horizontal.start as i16 - 1));
            }
        }

        for (layer, covers) in covers_left_of_row {
            if covers.iter().any(|&cover| !cover.eq(i8x16::splat(0)).all()) {
                self.queue.push_back(CoverCarry { covers, layer });
            }
        }

        for (i, tile) in row.enumerate() {
            if let Some(rect) = &crop {
                if !rect.horizontal.contains(&i) {
                    continue;
                }
            }

            let current_segments =
                search_last_by_key(segments, i as i16, |segment| segment.tile_i())
                    .map(|last_index| {
                        let current_segments = &segments[..=last_index];
                        segments = &segments[last_index + 1..];
                        current_segments
                    })
                    .unwrap_or(&[]);

            let is_unchanged = layers_per_tile
                .map(|layers_per_tile| {
                    self.is_unchanged(layers_per_tile.entry((i, j)), current_segments, styles)
                })
                .unwrap_or_default();

            if current_segments.is_empty() && self.queue.is_empty() {
                if !is_unchanged {
                    self.write_to_tile(tile, Some(to_bytes(clear_color)), flusher);
                }

                continue;
            }

            if !is_unchanged {
                if let Some(color) =
                    self.top_carry_layers_solid_opaque(current_segments, styles, clear_color)
                {
                    self.write_to_tile(tile, Some(to_bytes(color)), flusher);
                } else {
                    self.clear(clear_color);

                    self.paint_tile(i, j, current_segments, styles);
                    self.compute_srgb();

                    self.write_to_tile(tile, None, flusher);
                }
            } else {
                let mut covers = BTreeMap::new();

                acc_covers(current_segments, &mut covers);

                for cover_carry in self.queue.drain(..) {
                    let covers = covers
                        .entry(cover_carry.layer)
                        .or_insert_with(|| [i8x16::splat(0); TILE_SIZE / 16]);

                    for (dst, src) in covers.iter_mut().zip(cover_carry.covers.iter()) {
                        *dst += *src;
                    }
                }

                for (layer, covers) in covers {
                    if covers.iter().any(|&cover| !cover.eq(i8x16::splat(0)).all()) {
                        self.next_queue.push_back(CoverCarry { covers, layer });
                    }
                }
            }

            mem::swap(&mut self.queue, &mut self.next_queue);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::collections::HashMap;

    use crate::{point::Point, rasterizer::Rasterizer, LinesBuilder, Segment, TILE_SIZE};

    const BLACK: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
    const BLACK_TRANSPARENT: [f32; 4] = [0.0, 0.0, 0.0, 0.5];
    const RED: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
    const RED_50: [f32; 4] = [0.5, 0.0, 0.0, 1.0];
    const GREEN: [f32; 4] = [0.0, 1.0, 0.0, 1.0];
    const GREEN_50: [f32; 4] = [0.0, 0.5, 0.0, 1.0];
    const RED_GREEN_50: [f32; 4] = [0.5, 0.5, 0.0, 1.0];

    impl Painter {
        fn colors(&self) -> [[f32; 4]; TILE_SIZE * TILE_SIZE] {
            let mut colors = [[0.0, 0.0, 0.0, 1.0]; TILE_SIZE * TILE_SIZE];

            for (i, (((&c0, &c1), &c2), &alpha)) in self
                .c0
                .iter()
                .flat_map(f32x8::as_array)
                .zip(self.c1.iter().flat_map(f32x8::as_array))
                .zip(self.c2.iter().flat_map(f32x8::as_array))
                .zip(self.alpha.iter().flat_map(f32x8::as_array))
                .enumerate()
            {
                colors[i] = [c0, c1, c2, alpha];
            }

            colors
        }
    }

    fn line_segments(points: &[(Point<f32>, Point<f32>)], same_layer: bool) -> Vec<CompactSegment> {
        let mut builder = LinesBuilder::new();

        for (layer, &(p0, p1)) in points.iter().enumerate() {
            let layer = if same_layer { 0 } else { layer };
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
        let mut cover_carry =
            CoverCarry { covers: [i8x16::splat(0); TILE_SIZE / i8x16::LANES], layer: 0 };
        cover_carry.covers[0].as_mut_array()[1] = 16;
        cover_carry.layer = 1;

        let segments =
            line_segments(&[(Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32))], false);

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

        painter.paint_tile(0, 0, &segments, &|order| styles[&order].clone());

        assert_eq!(painter.colors()[0..2], [GREEN, RED]);
    }

    #[test]
    fn overlapping_triangles() {
        let segments = line_segments(
            &[
                (Point::new(0.0, 0.0), Point::new(TILE_SIZE as f32, TILE_SIZE as f32)),
                (Point::new(TILE_SIZE as f32, 0.0), Point::new(0.0, TILE_SIZE as f32)),
            ],
            false,
        );

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
        painter.paint_tile(0, 0, &segments, &|order| styles[&order].clone());

        let row_start = TILE_SIZE / 2 - 2;
        let row_end = TILE_SIZE / 2 + 2;

        let mut column = (TILE_SIZE / 2 - 2) * TILE_SIZE;
        assert_eq!(
            painter.colors()[column + row_start..column + row_end],
            [GREEN_50, BLACK, BLACK, RED_50]
        );

        column += TILE_SIZE;
        assert_eq!(
            painter.colors()[column + row_start..column + row_end],
            [GREEN, GREEN_50, RED_50, RED]
        );

        column += TILE_SIZE;
        assert_eq!(
            painter.colors()[column + row_start..column + row_end],
            [GREEN, RED_GREEN_50, RED, RED]
        );

        column += TILE_SIZE;
        assert_eq!(
            painter.colors()[column + row_start..column + row_end],
            [RED_GREEN_50, RED, RED, RED]
        );
    }

    #[test]
    fn transparent_overlay() {
        let segments = line_segments(
            &[
                (Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32)),
                (Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32)),
            ],
            false,
        );

        let mut styles = HashMap::new();

        styles.insert(
            0,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(RED),
                blend_mode: BlendMode::Over,
            },
        );
        styles.insert(
            1,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(BLACK_TRANSPARENT),
                blend_mode: BlendMode::Over,
            },
        );

        let mut painter = Painter::new();
        painter.paint_tile(0, 0, &segments, &|order| styles[&order].clone());

        assert_eq!(painter.colors()[0], RED_50);
    }

    #[test]
    fn cover_carry_is_empty() {
        assert!(CoverCarry { covers: [i8x16::splat(0); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::NonZero));
        assert!(!CoverCarry { covers: [i8x16::splat(1); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::NonZero));
        assert!(!CoverCarry { covers: [i8x16::splat(-1); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::NonZero));
        assert!(!CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::NonZero));
        assert!(!CoverCarry { covers: [i8x16::splat(-16); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::NonZero));

        assert!(CoverCarry { covers: [i8x16::splat(0); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(1); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(-1); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(-16); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(CoverCarry { covers: [i8x16::splat(32); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(CoverCarry { covers: [i8x16::splat(-32); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(48); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(-48); TILE_SIZE / 16], layer: 0 }
            .is_empty(FillRule::EvenOdd));
    }

    #[test]
    fn cover_carry_is_full() {
        assert!(!CoverCarry { covers: [i8x16::splat(0); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::NonZero));
        assert!(!CoverCarry { covers: [i8x16::splat(1); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::NonZero));
        assert!(!CoverCarry { covers: [i8x16::splat(-1); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::NonZero));
        assert!(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::NonZero));
        assert!(CoverCarry { covers: [i8x16::splat(-16); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::NonZero));

        assert!(!CoverCarry { covers: [i8x16::splat(0); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(1); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(-1); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(CoverCarry { covers: [i8x16::splat(-16); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(32); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(!CoverCarry { covers: [i8x16::splat(-32); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(CoverCarry { covers: [i8x16::splat(48); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
        assert!(CoverCarry { covers: [i8x16::splat(-48); TILE_SIZE / 16], layer: 0 }
            .is_full(FillRule::EvenOdd));
    }

    #[test]
    fn top_carry_layers_solid_opaque_incomplete() {
        let mut painter = Painter::new();

        let mut queue = VecDeque::new();

        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(15); TILE_SIZE / 16], layer: 1 });

        painter.queue = queue;

        let result = painter.top_carry_layers_solid_opaque(&[], &|_| Style::default(), [0.0; 4]);

        assert_eq!(result, None);
    }

    #[test]
    fn top_carry_layers_solid_opaque_segments_on_top() {
        let mut painter = Painter::new();

        let mut queue = VecDeque::new();

        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 });

        painter.queue = queue;

        let result = painter.top_carry_layers_solid_opaque(
            &[CompactSegment::new(0, 0, 0, 1, 0, 0, 0, 0)],
            &|_| Style::default(),
            [0.0; 4],
        );

        assert_eq!(result, None);
    }

    #[test]
    fn top_carry_layers_solid_opaque_blend() {
        let mut painter = Painter::new();

        let mut queue = VecDeque::new();

        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 0 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 1 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 2 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 3 });

        painter.queue = queue;

        let result = painter.top_carry_layers_solid_opaque(
            &[],
            &|_| Style {
                fill: Fill::Solid([0.5, 0.5, 0.5, 1.0]),
                blend_mode: BlendMode::Multiply,
                ..Default::default()
            },
            [1.0; 4],
        );

        assert_eq!(result, Some([0.0625, 0.0625, 0.0625, 1.0]));
    }

    #[test]
    fn top_carry_layers_solid_opaque_bottom_layer_not_opaque() {
        let mut painter = Painter::new();

        let mut queue = VecDeque::new();

        queue.push_back(CoverCarry { covers: [i8x16::splat(15); TILE_SIZE / 16], layer: 0 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 1 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 2 });

        painter.queue = queue;

        let result = painter.top_carry_layers_solid_opaque(
            &[],
            &|order| {
                if order == 1 {
                    Style { blend_mode: BlendMode::Difference, ..Default::default() }
                } else {
                    Style::default()
                }
            },
            [0.0; 4],
        );

        assert_eq!(result, None);

        let result = painter.top_carry_layers_solid_opaque(
            &[],
            &|order| {
                if order == 1 {
                    Style { fill: Fill::Solid([0.9; 4]), ..Default::default() }
                } else {
                    Style::default()
                }
            },
            [0.0; 4],
        );

        assert_eq!(result, None);
    }

    #[test]
    fn top_carry_layers_solid_opaque_overlay_not_solid() {
        let mut painter = Painter::new();

        let mut queue = VecDeque::new();

        queue.push_back(CoverCarry { covers: [i8x16::splat(15); TILE_SIZE / 16], layer: 0 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 1 });
        queue.push_back(CoverCarry { covers: [i8x16::splat(16); TILE_SIZE / 16], layer: 2 });

        painter.queue = queue;

        let mut builder = GradientBuilder::new([0.0; 2], [0.0; 2]);
        builder.color([0.1; 4]).color([0.2; 4]);
        let gradient = builder.build().unwrap();

        let result = painter.top_carry_layers_solid_opaque(
            &[],
            &|order| {
                if order == 2 {
                    Style { fill: Fill::Gradient(gradient.clone()), ..Default::default() }
                } else {
                    Style::default()
                }
            },
            [0.0; 4],
        );

        assert_eq!(result, None);
    }
}
