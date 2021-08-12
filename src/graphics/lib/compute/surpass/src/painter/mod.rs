// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{borrow::Cow, cell::Cell, collections::BTreeMap, mem, slice::ChunksExactMut};

use crate::{
    painter::layer_workbench::TileWriteOp,
    rasterizer::{search_last_by_key, CompactSegment},
    simd::{f32x8, i16x16, i32x8, i8x16, u8x32, u8x8, Simd},
    PIXEL_WIDTH, TILE_SIZE,
};

mod buffer_layout;
mod layer_workbench;
#[macro_use]
mod style;

use buffer_layout::TileSlice;
pub use buffer_layout::{BufferLayout, BufferLayoutBuilder, Flusher, Rect};

use layer_workbench::{Context, LayerPainter, LayerWorkbench};

pub use style::{BlendMode, Fill, FillRule, Gradient, GradientBuilder, GradientType, Style};

const LAST_BYTE_MASK: i32 = 0b1111_1111;
const LAST_BIT_MASK: i32 = 0b1;

macro_rules! cols {
    ( & $array:expr, $x0:expr, $x1:expr ) => {{
        fn size_of_el<T: Simd>(_: impl AsRef<[T]>) -> usize {
            T::LANES
        }

        let from = $x0 * crate::TILE_SIZE / size_of_el(&$array);
        let to = $x1 * crate::TILE_SIZE / size_of_el(&$array);

        &$array[from..to]
    }};

    ( & mut $array:expr, $x0:expr, $x1:expr ) => {{
        fn size_of_el<T: Simd>(_: impl AsRef<[T]>) -> usize {
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

#[derive(Clone, Debug, PartialEq)]
pub enum Func {
    Draw(Style),
    Clip(usize),
}

impl Default for Func {
    fn default() -> Self {
        Self::Draw(Style::default())
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Props {
    pub fill_rule: FillRule,
    pub func: Func,
}

pub trait LayerProps: Send + Sync {
    fn get(&self, layer: u16) -> Cow<'_, Props>;
    fn is_unchanged(&self, layer: u16) -> bool;
}

#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct Cover {
    covers: [i8x16; TILE_SIZE / i8x16::LANES],
}

impl Cover {
    pub fn as_slice_mut(&mut self) -> &mut [i8; TILE_SIZE] {
        unsafe { mem::transmute(&mut self.covers) }
    }

    pub fn add_cover_to(&self, covers: &mut [i8x16]) {
        for (i, &cover) in self.covers.iter().enumerate() {
            covers[i] += cover;
        }
    }

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

impl PartialEq for Cover {
    fn eq(&self, other: &Self) -> bool {
        self.covers.iter().zip(other.covers.iter()).all(|(t, o)| t.eq(*o).all())
    }
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct CoverCarry {
    cover: Cover,
    layer: u16,
}

#[derive(Debug)]
pub(crate) struct Painter {
    areas: [i16x16; TILE_SIZE * TILE_SIZE / i16x16::LANES],
    covers: [i8x16; (TILE_SIZE + 1) * TILE_SIZE / i8x16::LANES],
    clip: Option<([f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES], u16)>,
    c0: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    c1: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    c2: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    alpha: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    srgb: [u8x32; TILE_SIZE * TILE_SIZE * 4 / u8x32::LANES],
}

impl LayerPainter for Painter {
    fn clear_cells(&mut self) {
        self.areas.iter_mut().for_each(|area| *area = i16x16::splat(0));
        self.covers.iter_mut().for_each(|cover| *cover = i8x16::splat(0));
    }

    fn acc_segment(&mut self, segment: CompactSegment) {
        let x = segment.tile_x() as usize;
        let y = segment.tile_y() as usize;

        let areas: &mut [i16; TILE_SIZE * TILE_SIZE] = unsafe { mem::transmute(&mut self.areas) };
        let covers: &mut [i8; (TILE_SIZE + 1) * TILE_SIZE] =
            unsafe { mem::transmute(&mut self.covers) };

        areas[x * TILE_SIZE + y] += segment.area();
        covers[(x + 1) * TILE_SIZE + y] += segment.cover();
    }

    fn acc_cover(&mut self, cover: Cover) {
        cover.add_cover_to(&mut self.covers);
    }

    fn clear(&mut self, color: [f32; 4]) {
        self.c0.iter_mut().for_each(|c0| *c0 = f32x8::splat(color[0]));
        self.c1.iter_mut().for_each(|c1| *c1 = f32x8::splat(color[1]));
        self.c2.iter_mut().for_each(|c2| *c2 = f32x8::splat(color[2]));
        self.alpha.iter_mut().for_each(|alpha| *alpha = f32x8::splat(color[3]));
    }

    fn paint_layer(
        &mut self,
        tile_i: usize,
        tile_j: usize,
        layer: u16,
        props: &Props,
        apply_clip: bool,
    ) -> Cover {
        let mut areas = [i32x8::splat(0); TILE_SIZE / i32x8::LANES];
        let mut covers = [i8x16::splat(0); TILE_SIZE / i8x16::LANES];
        let mut coverages = [f32x8::splat(0.0); TILE_SIZE / f32x8::LANES];

        if let Some((_, last_layer)) = self.clip {
            if last_layer < layer {
                self.clip = None;
            }
        }

        for x in 0..=TILE_SIZE {
            if x != 0 {
                self.compute_areas(x - 1, &covers, &mut areas);

                for y in 0..coverages.len() {
                    coverages[y] = from_area(areas[y], props.fill_rule);

                    match &props.func {
                        Func::Draw(style) => {
                            if coverages[y].eq(f32x8::splat(0.0)).all() {
                                continue;
                            }

                            if apply_clip && self.clip.is_none() {
                                continue;
                            }

                            let fill = Self::fill_at(
                                x + tile_i * TILE_SIZE,
                                y * f32x8::LANES + tile_j * TILE_SIZE,
                                &style,
                            );

                            self.blend_at(x - 1, y, coverages, apply_clip, fill, style.blend_mode);
                        }
                        Func::Clip(layers) => {
                            self.clip_at(x - 1, y, coverages, layer + *layers as u16)
                        }
                    }
                }
            }

            let column = cols!(&self.covers, x, x + 1);
            for y in 0..column.len() {
                covers[y] += column[y];
            }
        }

        Cover { covers }
    }
}

impl Painter {
    pub fn new() -> Self {
        Self {
            areas: [i16x16::splat(0); TILE_SIZE * TILE_SIZE / i16x16::LANES],
            covers: [i8x16::splat(0); (TILE_SIZE + 1) * TILE_SIZE / i8x16::LANES],
            clip: None,
            c0: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            c1: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            c2: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            alpha: [f32x8::splat(1.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            srgb: [u8x32::splat(0); TILE_SIZE * TILE_SIZE * 4 / u8x32::LANES],
        }
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

    fn blend_at(
        &mut self,
        x: usize,
        y: usize,
        coverages: [f32x8; TILE_SIZE / f32x8::LANES],
        is_clipped: bool,
        fill: [f32x8; 4],
        blend_mode: BlendMode,
    ) {
        let c0 = cols!(&mut self.c0, x, x + 1);
        let c1 = cols!(&mut self.c1, x, x + 1);
        let c2 = cols!(&mut self.c2, x, x + 1);
        let alpha = cols!(&mut self.alpha, x, x + 1);

        let mut alphas = fill[3] * coverages[y];

        if is_clipped {
            if let Some((mask, _)) = self.clip {
                alphas *= cols!(&mask, x, x + 1)[y];
            }
        }

        let inv_alphas = f32x8::splat(1.0) - alphas;

        let [mut current_c0, mut current_c1, mut current_c2] =
            blend_function!(blend_mode, c0[y], c1[y], c2[y], fill[0], fill[1], fill[2],);

        current_c0 *= alphas;
        current_c1 *= alphas;
        current_c2 *= alphas;
        let current_alpha = alphas;

        c0[y] = c0[y].mul_add(inv_alphas, current_c0);
        c1[y] = c1[y].mul_add(inv_alphas, current_c1);
        c2[y] = c2[y].mul_add(inv_alphas, current_c2);
        alpha[y] = alpha[y].mul_add(inv_alphas, current_alpha);
    }

    fn clip_at(
        &mut self,
        x: usize,
        y: usize,
        coverages: [f32x8; TILE_SIZE / f32x8::LANES],
        last_layer: u16,
    ) {
        let clip = self.clip.get_or_insert_with(|| {
            ([f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES], last_layer)
        });
        cols!(&mut clip.0, x, x + 1)[y] = coverages[y];
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
        &mut self,
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
            self.compute_srgb();
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

    pub fn paint_tile_row<'c, P: LayerProps>(
        &mut self,
        workbench: &mut LayerWorkbench,
        j: usize,
        mut segments: &[CompactSegment],
        props: &P,
        clear_color: [f32; 4],
        mut previous_layers: Option<&mut [Option<u16>]>,
        flusher: Option<&dyn Flusher>,
        row: ChunksExactMut<'_, TileSlice>,
        crop: Option<Rect>,
    ) {
        fn acc_covers(segments: &[CompactSegment], covers: &mut BTreeMap<u16, Cover>) {
            for segment in segments {
                let cover = covers.entry(segment.layer()).or_default();

                cover.as_slice_mut()[segment.tile_y() as usize] += segment.cover();
            }
        }

        let mut covers_left_of_row: BTreeMap<u16, Cover> = BTreeMap::new();
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

        workbench
            .init(covers_left_of_row.into_iter().map(|(layer, cover)| CoverCarry { cover, layer }));

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

            let context = Context {
                tile_i: i,
                tile_j: j,
                segments: current_segments,
                props,
                previous_layers: Cell::new(
                    previous_layers.as_mut().map(|layers_per_tile| &mut layers_per_tile[i]),
                ),
                clear_color,
            };

            self.clip = None;

            match workbench.drive_tile_painting(self, &context) {
                TileWriteOp::None => (),
                TileWriteOp::Solid(color) => {
                    self.write_to_tile(tile, Some(to_bytes(color)), flusher)
                }
                TileWriteOp::ColorBuffer => {
                    self.write_to_tile(tile, None, flusher);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::collections::HashMap;

    use crate::{
        point::Point,
        rasterizer::{self, Rasterizer},
        LinesBuilder, Segment, TILE_SIZE,
    };

    const BLACK: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
    const BLACK_TRANSPARENT: [f32; 4] = [0.0, 0.0, 0.0, 0.5];
    const RED: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
    const RED_50: [f32; 4] = [0.5, 0.0, 0.0, 1.0];
    const GREEN: [f32; 4] = [0.0, 1.0, 0.0, 1.0];
    const GREEN_50: [f32; 4] = [0.0, 0.5, 0.0, 1.0];
    const RED_GREEN_50: [f32; 4] = [0.5, 0.5, 0.0, 1.0];

    impl LayerProps for HashMap<u16, Style> {
        fn get(&self, layer: u16) -> Cow<'_, Props> {
            let style = self.get(&layer).unwrap().clone();

            Cow::Owned(Props { fill_rule: FillRule::NonZero, func: Func::Draw(style) })
        }

        fn is_unchanged(&self, _: u16) -> bool {
            false
        }
    }

    impl LayerProps for HashMap<u16, Props> {
        fn get(&self, layer: u16) -> Cow<'_, Props> {
            Cow::Owned(self.get(&layer).unwrap().clone())
        }

        fn is_unchanged(&self, _: u16) -> bool {
            false
        }
    }

    impl<F> LayerProps for F
    where
        F: Fn(u16) -> Style + Send + Sync,
    {
        fn get(&self, layer: u16) -> Cow<'_, Props> {
            let style = self(layer);

            Cow::Owned(Props { fill_rule: FillRule::NonZero, func: Func::Draw(style) })
        }

        fn is_unchanged(&self, _: u16) -> bool {
            false
        }
    }

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

        let last_segment =
            rasterizer::search_last_by_key(&segments, 0, |segment| segment.is_none()).unwrap_or(0);
        segments.truncate(last_segment + 1);
        segments
    }

    fn paint_tile(
        cover_carries: impl IntoIterator<Item = CoverCarry>,
        segments: &[CompactSegment],
        props: &impl LayerProps,
    ) -> [[f32; 4]; TILE_SIZE * TILE_SIZE] {
        let mut painter = Painter::new();
        let mut workbench = LayerWorkbench::new();

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments,
            props,
            previous_layers: Cell::new(None),
            clear_color: BLACK,
        };

        workbench.init(cover_carries);
        workbench.drive_tile_painting(&mut painter, &context);

        painter.colors()
    }

    #[test]
    fn carry_cover() {
        let mut cover_carry = CoverCarry {
            cover: Cover { covers: [i8x16::splat(0); TILE_SIZE / i8x16::LANES] },
            layer: 0,
        };
        cover_carry.cover.covers[0].as_mut_array()[1] = 16;
        cover_carry.layer = 1;

        let segments =
            line_segments(&[(Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32))], false);

        let mut styles = HashMap::new();

        styles.insert(0, Style { fill: Fill::Solid(GREEN), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(RED), ..Default::default() });

        assert_eq!(paint_tile([cover_carry], &segments, &styles)[0..2], [GREEN, RED]);
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

        styles.insert(0, Style { fill: Fill::Solid(GREEN), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(RED), ..Default::default() });

        let colors = paint_tile([], &segments, &styles);

        let row_start = TILE_SIZE / 2 - 2;
        let row_end = TILE_SIZE / 2 + 2;

        let mut column = (TILE_SIZE / 2 - 2) * TILE_SIZE;
        assert_eq!(colors[column + row_start..column + row_end], [GREEN_50, BLACK, BLACK, RED_50]);

        column += TILE_SIZE;
        assert_eq!(colors[column + row_start..column + row_end], [GREEN, GREEN_50, RED_50, RED]);

        column += TILE_SIZE;
        assert_eq!(colors[column + row_start..column + row_end], [GREEN, RED_GREEN_50, RED, RED]);

        column += TILE_SIZE;
        assert_eq!(colors[column + row_start..column + row_end], [RED_GREEN_50, RED, RED, RED]);
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

        styles.insert(0, Style { fill: Fill::Solid(RED), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(BLACK_TRANSPARENT), ..Default::default() });

        assert_eq!(paint_tile([], &segments, &styles)[0], RED_50);
    }

    #[test]
    fn cover_carry_is_empty() {
        assert!(Cover { covers: [i8x16::splat(0); TILE_SIZE / 16] }.is_empty(FillRule::NonZero));
        assert!(!Cover { covers: [i8x16::splat(1); TILE_SIZE / 16] }.is_empty(FillRule::NonZero));
        assert!(!Cover { covers: [i8x16::splat(-1); TILE_SIZE / 16] }.is_empty(FillRule::NonZero));
        assert!(!Cover { covers: [i8x16::splat(16); TILE_SIZE / 16] }.is_empty(FillRule::NonZero));
        assert!(!Cover { covers: [i8x16::splat(-16); TILE_SIZE / 16] }.is_empty(FillRule::NonZero));

        assert!(Cover { covers: [i8x16::splat(0); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(1); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(-1); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(16); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(-16); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(Cover { covers: [i8x16::splat(32); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(Cover { covers: [i8x16::splat(-32); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(48); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(-48); TILE_SIZE / 16] }.is_empty(FillRule::EvenOdd));
    }

    #[test]
    fn cover_carry_is_full() {
        assert!(!Cover { covers: [i8x16::splat(0); TILE_SIZE / 16] }.is_full(FillRule::NonZero));
        assert!(!Cover { covers: [i8x16::splat(1); TILE_SIZE / 16] }.is_full(FillRule::NonZero));
        assert!(!Cover { covers: [i8x16::splat(-1); TILE_SIZE / 16] }.is_full(FillRule::NonZero));
        assert!(Cover { covers: [i8x16::splat(16); TILE_SIZE / 16] }.is_full(FillRule::NonZero));
        assert!(Cover { covers: [i8x16::splat(-16); TILE_SIZE / 16] }.is_full(FillRule::NonZero));

        assert!(!Cover { covers: [i8x16::splat(0); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(1); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(-1); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(Cover { covers: [i8x16::splat(16); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(Cover { covers: [i8x16::splat(-16); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(32); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(!Cover { covers: [i8x16::splat(-32); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(Cover { covers: [i8x16::splat(48); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
        assert!(Cover { covers: [i8x16::splat(-48); TILE_SIZE / 16] }.is_full(FillRule::EvenOdd));
    }

    #[test]
    fn clip() {
        let segments = line_segments(
            &[
                (Point::new(0.0, 0.0), Point::new(TILE_SIZE as f32, TILE_SIZE as f32)),
                (Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32)),
                (Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32)),
            ],
            false,
        );

        let mut props = HashMap::new();

        props.insert(0, Props { fill_rule: FillRule::NonZero, func: Func::Clip(2) });
        props.insert(
            1,
            Props {
                fill_rule: FillRule::NonZero,
                func: Func::Draw(Style {
                    fill: Fill::Solid(GREEN),
                    is_clipped: true,
                    ..Default::default()
                }),
            },
        );
        props.insert(
            2,
            Props {
                fill_rule: FillRule::NonZero,
                func: Func::Draw(Style {
                    fill: Fill::Solid(RED),
                    is_clipped: true,
                    ..Default::default()
                }),
            },
        );
        props.insert(
            3,
            Props {
                fill_rule: FillRule::NonZero,
                func: Func::Draw(Style { fill: Fill::Solid(GREEN), ..Default::default() }),
            },
        );

        let mut painter = Painter::new();
        let mut workbench = LayerWorkbench::new();

        let mut context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &segments,
            props: &props,
            previous_layers: Cell::new(None),
            clear_color: BLACK,
        };

        workbench.drive_tile_painting(&mut painter, &context);

        let colors = painter.colors();
        let mut col = [BLACK; TILE_SIZE];

        for i in 0..TILE_SIZE {
            col[i] = [0.5, 0.25, 0.0, 1.0];

            if i >= 1 {
                col[i - 1] = RED;
            }

            assert_eq!(colors[i * TILE_SIZE..(i + 1) * TILE_SIZE], col);
        }

        let segments = line_segments(
            &[(Point::new(TILE_SIZE as f32, 0.0), Point::new(TILE_SIZE as f32, TILE_SIZE as f32))],
            false,
        );

        context.tile_i = 1;
        context.segments = &segments;

        workbench.drive_tile_painting(&mut painter, &context);

        assert_eq!(painter.colors(), [RED; TILE_SIZE * TILE_SIZE]);
    }
}
