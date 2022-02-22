// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    borrow::Cow,
    cell::{Cell, RefCell, RefMut},
    collections::BTreeMap,
    mem,
    ops::Range,
    slice::ChunksExactMut,
};

use rayon::prelude::*;

use crate::{
    layout::{Flusher, Layout, Slice, TileFill},
    painter::layer_workbench::TileWriteOp,
    rasterizer::{search_last_by_key, PixelSegment},
    simd::{f32x4, f32x8, i16x16, i32x8, i8x16, u32x4, u32x8, u8x32, Simd},
    PIXEL_DOUBLE_WIDTH, PIXEL_WIDTH, TILE_SIZE,
};

mod layer_workbench;
#[macro_use]
mod style;

use layer_workbench::{Context, LayerPainter, LayerWorkbench};

pub use style::{BlendMode, Fill, FillRule, Gradient, GradientBuilder, GradientType, Style};

pub use self::style::{Channel, Color, BGRA, RGBA};

const PIXEL_AREA: usize = PIXEL_WIDTH * PIXEL_WIDTH;
const PIXEL_DOUBLE_AREA: usize = 2 * PIXEL_AREA;

const MAGNITUDE_BIT_LEN: usize = PIXEL_DOUBLE_AREA.trailing_zeros() as usize;
const MAGNITUDE_MASK: usize = MAGNITUDE_BIT_LEN - 1;

// From Hacker's Delight, p. 378-380. 2 ^ 23 as f32.
const C23: u32 = 0x4B00_0000;

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
fn doubled_area_to_coverage(doubled_area: i32x8, fill_rule: FillRule) -> f32x8 {
    match fill_rule {
        FillRule::NonZero => {
            let doubled_area: f32x8 = doubled_area.into();
            (doubled_area * f32x8::splat((PIXEL_DOUBLE_AREA as f32).recip()))
                .abs()
                .clamp(f32x8::splat(0.0), f32x8::splat(1.0))
        }
        FillRule::EvenOdd => {
            let winding_number = doubled_area.shr::<{ MAGNITUDE_BIT_LEN as i32 }>();
            let magnitude: f32x8 = (doubled_area & i32x8::splat(MAGNITUDE_MASK as i32)).into();
            let norm = magnitude * f32x8::splat((PIXEL_DOUBLE_AREA as f32).recip());

            let mask = (winding_number & i32x8::splat(0b1)).eq(i32x8::splat(0));
            norm.select(f32x8::splat(1.0) - norm, mask)
        }
    }
}

#[allow(clippy::many_single_char_names)]
#[inline]
fn linear_to_srgb_approx_simdx8(l: f32x8) -> f32x8 {
    let a = f32x8::splat(0.201_017_72f32);
    let b = f32x8::splat(-0.512_801_47f32);
    let c = f32x8::splat(1.344_401f32);
    let d = f32x8::splat(-0.030_656_587f32);

    let s = l.sqrt();
    let s2 = l;
    let s3 = s2 * s;

    let m = l * f32x8::splat(12.92);
    let n = a.mul_add(s3, b.mul_add(s2, c.mul_add(s, d)));

    m.select(n, l.le(f32x8::splat(0.003_130_8)))
}

#[allow(clippy::many_single_char_names)]
#[inline]
fn linear_to_srgb_approx_simdx4(l: f32x4) -> f32x4 {
    let a = f32x4::splat(0.201_017_72f32);
    let b = f32x4::splat(-0.512_801_47f32);
    let c = f32x4::splat(1.344_401f32);
    let d = f32x4::splat(-0.030_656_587f32);

    let s = l.sqrt();
    let s2 = l;
    let s3 = s2 * s;

    let m = l * f32x4::splat(12.92);
    let n = a.mul_add(s3, b.mul_add(s2, c.mul_add(s, d)));

    m.select(n, l.le(f32x4::splat(0.003_130_8)))
}

// From Hacker's Delight, p. 378-380.

#[inline]
fn to_u32x8(val: f32x8) -> u32x8 {
    let max = f32x8::splat(u8::MAX as f32);
    let c23 = u32x8::splat(C23);

    let scaled = (val * max).clamp(f32x8::splat(0.0), max);
    let val = scaled + f32x8::from_bits(c23);

    val.to_bits()
}

#[inline]
fn to_u32x4(val: f32x4) -> u32x4 {
    let max = f32x4::splat(u8::MAX as f32);
    let c23 = u32x4::splat(C23);

    let scaled = (val * max).clamp(f32x4::splat(0.0), max);
    let val = scaled + f32x4::from_bits(c23);

    val.to_bits()
}

#[inline]
fn to_srgb_bytes(color: [f32; 4]) -> [u8; 4] {
    let alpha_recip = color[3].recip();
    let srgb = linear_to_srgb_approx_simdx4(f32x4::new([
        color[0] * alpha_recip,
        color[1] * alpha_recip,
        color[2] * alpha_recip,
        0.0,
    ]));

    let srgb = to_u32x4(srgb.insert::<3>(color[3]));

    srgb.into()
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct Rect {
    pub(crate) hor: Range<usize>,
    pub(crate) vert: Range<usize>,
}

impl Rect {
    pub fn new(horizontal: Range<usize>, vertical: Range<usize>) -> Self {
        Self {
            hor: horizontal.start / TILE_SIZE..(horizontal.end + TILE_SIZE - 1) / TILE_SIZE,
            vert: vertical.start / TILE_SIZE..(vertical.end + TILE_SIZE - 1) / TILE_SIZE,
        }
    }
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
    fn get(&self, layer_id: u32) -> Cow<'_, Props>;
    fn is_unchanged(&self, layer_id: u32) -> bool;
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
    layer_id: u32,
}

#[derive(Debug)]
pub(crate) struct Painter {
    doubled_areas: [i16x16; TILE_SIZE * TILE_SIZE / i16x16::LANES],
    covers: [i8x16; (TILE_SIZE + 1) * TILE_SIZE / i8x16::LANES],
    clip: Option<([f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES], u32)>,
    red: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    green: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    blue: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    alpha: [f32x8; TILE_SIZE * TILE_SIZE / f32x8::LANES],
    srgb: [u8x32; TILE_SIZE * TILE_SIZE * 4 / u8x32::LANES],
}

impl LayerPainter for Painter {
    fn clear_cells(&mut self) {
        self.doubled_areas.iter_mut().for_each(|doubled_area| *doubled_area = i16x16::splat(0));
        self.covers.iter_mut().for_each(|cover| *cover = i8x16::splat(0));
    }

    fn acc_segment(&mut self, segment: PixelSegment) {
        let x = segment.local_x() as usize;
        let y = segment.local_y() as usize;

        let doubled_areas: &mut [i16; TILE_SIZE * TILE_SIZE] =
            unsafe { mem::transmute(&mut self.doubled_areas) };
        let covers: &mut [i8; (TILE_SIZE + 1) * TILE_SIZE] =
            unsafe { mem::transmute(&mut self.covers) };

        doubled_areas[x * TILE_SIZE + y] += segment.double_area();
        covers[(x + 1) * TILE_SIZE + y] += segment.cover();
    }

    fn acc_cover(&mut self, cover: Cover) {
        cover.add_cover_to(&mut self.covers);
    }

    fn clear(&mut self, color: Color) {
        self.red.iter_mut().for_each(|r| *r = f32x8::splat(color.r));
        self.green.iter_mut().for_each(|g| *g = f32x8::splat(color.g));
        self.blue.iter_mut().for_each(|b| *b = f32x8::splat(color.b));
        self.alpha.iter_mut().for_each(|alpha| *alpha = f32x8::splat(color.a));
    }

    fn paint_layer(
        &mut self,
        tile_x: usize,
        tile_y: usize,
        layer_id: u32,
        props: &Props,
        apply_clip: bool,
    ) -> Cover {
        let mut doubled_areas = [i32x8::splat(0); TILE_SIZE / i32x8::LANES];
        let mut covers = [i8x16::splat(0); TILE_SIZE / i8x16::LANES];
        let mut coverages = [f32x8::splat(0.0); TILE_SIZE / f32x8::LANES];

        if let Some((_, last_layer)) = self.clip {
            if last_layer < layer_id {
                self.clip = None;
            }
        }

        for x in 0..=TILE_SIZE {
            if x != 0 {
                self.compute_doubled_areas(x - 1, &covers, &mut doubled_areas);

                for y in 0..coverages.len() {
                    coverages[y] = doubled_area_to_coverage(doubled_areas[y], props.fill_rule);

                    match &props.func {
                        Func::Draw(style) => {
                            if coverages[y].eq(f32x8::splat(0.0)).all() {
                                continue;
                            }

                            if apply_clip && self.clip.is_none() {
                                continue;
                            }

                            let fill = Self::fill_at(
                                x + tile_x * TILE_SIZE,
                                y * f32x8::LANES + tile_y * TILE_SIZE,
                                style,
                            );

                            self.blend_at(x - 1, y, coverages, apply_clip, fill, style.blend_mode);
                        }
                        Func::Clip(layers) => {
                            self.clip_at(x - 1, y, coverages, layer_id + *layers as u32)
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
            doubled_areas: [i16x16::splat(0); TILE_SIZE * TILE_SIZE / i16x16::LANES],
            covers: [i8x16::splat(0); (TILE_SIZE + 1) * TILE_SIZE / i8x16::LANES],
            clip: None,
            red: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            green: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            blue: [f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            alpha: [f32x8::splat(1.0); TILE_SIZE * TILE_SIZE / f32x8::LANES],
            srgb: [u8x32::splat(0); TILE_SIZE * TILE_SIZE * 4 / u8x32::LANES],
        }
    }

    #[inline]
    fn fill_at(x: usize, y: usize, style: &Style) -> [f32x8; 4] {
        match &style.fill {
            Fill::Solid(color) => {
                let Color { r, g, b, a } = *color;
                [f32x8::splat(r), f32x8::splat(g), f32x8::splat(b), f32x8::splat(a)]
            }
            Fill::Gradient(gradient) => gradient.color_at(x as f32, y as f32),
        }
    }

    fn compute_doubled_areas(
        &self,
        x: usize,
        covers: &[i8x16; TILE_SIZE / i8x16::LANES],
        doubled_areas: &mut [i32x8; TILE_SIZE / i32x8::LANES],
    ) {
        let column = cols!(&self.doubled_areas, x, x + 1);
        for y in 0..covers.len() {
            let covers: [i32x8; 2] = covers[y].into();
            let column: [i32x8; 2] = column[y].into();

            for yy in 0..2 {
                doubled_areas[2 * y + yy] =
                    i32x8::splat(PIXEL_DOUBLE_WIDTH as i32) * covers[yy] + column[yy];
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
        let red = cols!(&mut self.red, x, x + 1);
        let green = cols!(&mut self.green, x, x + 1);
        let blue = cols!(&mut self.blue, x, x + 1);
        let alpha = cols!(&mut self.alpha, x, x + 1);

        let mut alphas = fill[3] * coverages[y];

        if is_clipped {
            if let Some((mask, _)) = self.clip {
                alphas *= cols!(&mask, x, x + 1)[y];
            }
        }

        let inv_alphas = f32x8::splat(1.0) - alphas;

        let [mut current_red, mut current_green, mut current_blue] =
            blend_function!(blend_mode, red[y], green[y], blue[y], fill[0], fill[1], fill[2],);

        current_red *= alphas;
        current_green *= alphas;
        current_blue *= alphas;
        let current_alpha = alphas;

        red[y] = red[y].mul_add(inv_alphas, current_red);
        green[y] = green[y].mul_add(inv_alphas, current_green);
        blue[y] = blue[y].mul_add(inv_alphas, current_blue);
        alpha[y] = alpha[y].mul_add(inv_alphas, current_alpha);
    }

    fn clip_at(
        &mut self,
        x: usize,
        y: usize,
        coverages: [f32x8; TILE_SIZE / f32x8::LANES],
        last_layer_id: u32,
    ) {
        let clip = self.clip.get_or_insert_with(|| {
            ([f32x8::splat(0.0); TILE_SIZE * TILE_SIZE / f32x8::LANES], last_layer_id)
        });
        cols!(&mut clip.0, x, x + 1)[y] = coverages[y];
    }

    fn compute_srgb(&mut self, channels: [Channel; 4]) {
        for (channel, alpha) in self.red.iter_mut().zip(self.alpha.iter()) {
            *channel = linear_to_srgb_approx_simdx8(*channel) * *alpha;
        }
        for (channel, alpha) in self.green.iter_mut().zip(self.alpha.iter()) {
            *channel = linear_to_srgb_approx_simdx8(*channel) * *alpha;
        }
        for (channel, alpha) in self.blue.iter_mut().zip(self.alpha.iter()) {
            *channel = linear_to_srgb_approx_simdx8(*channel) * *alpha;
        }

        for ((((&red, &green), &blue), &alpha), srgb) in self
            .red
            .iter()
            .zip(self.green.iter())
            .zip(self.blue.iter())
            .zip(self.alpha.iter())
            .zip(self.srgb.iter_mut())
        {
            let unpacked = channels.map(|c| to_u32x8(c.select(red, green, blue, alpha)));

            *srgb = u8x32::from_u32_interleaved(unpacked);
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub fn paint_tile_row<S: LayerProps, L: Layout>(
        &mut self,
        workbench: &mut LayerWorkbench,
        tile_y: usize,
        mut segments: &[PixelSegment],
        props: &S,
        channels: [Channel; 4],
        clear_color: Color,
        mut previous_layers: Option<&mut [Option<u32>]>,
        row: ChunksExactMut<'_, Slice<'_, u8>>,
        crop: Option<Rect>,
        flusher: Option<&dyn Flusher>,
    ) {
        fn acc_covers(segments: &[PixelSegment], covers: &mut BTreeMap<u32, Cover>) {
            for segment in segments {
                let cover = covers.entry(segment.layer_id()).or_default();

                cover.as_slice_mut()[segment.local_y() as usize] += segment.cover();
            }
        }

        let mut covers_left_of_row: BTreeMap<u32, Cover> = BTreeMap::new();
        let mut populate_covers = |limit: Option<i16>| {
            let query = search_last_by_key(segments, false, |segment| match limit {
                Some(limit) => (segment.tile_x() - limit).is_positive(),
                None => segment.tile_x().is_negative(),
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
            if rect.hor.start > 0 {
                populate_covers(Some(rect.hor.start as i16 - 1));
            }
        }

        workbench.init(
            covers_left_of_row
                .into_iter()
                .map(|(layer, cover)| CoverCarry { cover, layer_id: layer }),
        );

        for (tile_x, slices) in row.enumerate() {
            if let Some(rect) = &crop {
                if !rect.hor.contains(&tile_x) {
                    continue;
                }
            }

            let current_segments =
                search_last_by_key(segments, tile_x as i16, |segment| segment.tile_x())
                    .map(|last_index| {
                        let current_segments = &segments[..=last_index];
                        segments = &segments[last_index + 1..];
                        current_segments
                    })
                    .unwrap_or(&[]);

            let context = Context {
                tile_x,
                tile_y,
                segments: current_segments,
                props,
                previous_layers: Cell::new(
                    previous_layers.as_mut().map(|layers_per_tile| &mut layers_per_tile[tile_x]),
                ),
                clear_color,
            };

            self.clip = None;

            match workbench.drive_tile_painting(self, &context) {
                TileWriteOp::None => (),
                TileWriteOp::Solid(color) => {
                    let color = channels.map(|c| c.select_from_color(color));
                    L::write(slices, flusher, TileFill::Solid(to_srgb_bytes(color)))
                }
                TileWriteOp::ColorBuffer => {
                    self.compute_srgb(channels);
                    let colors: &[[u8; 4]] = unsafe {
                        std::slice::from_raw_parts(
                            self.srgb.as_ptr() as *const _,
                            self.srgb.len() * mem::size_of::<u8x32>() / mem::size_of::<[u8; 4]>(),
                        )
                    };
                    L::write(slices, flusher, TileFill::Full(colors));
                }
            }
        }
    }
}

thread_local!(static PAINTER_WORKBENCH: RefCell<(Painter, LayerWorkbench)> = RefCell::new((
    Painter::new(),
    LayerWorkbench::new(),
)));

#[allow(clippy::too_many_arguments)]
fn print_row<S: LayerProps, L: Layout>(
    segments: &[PixelSegment],
    channels: [Channel; 4],
    clear_color: Color,
    crop: &Option<Rect>,
    styles: &S,
    j: usize,
    row: ChunksExactMut<'_, Slice<'_, u8>>,
    layers_per_tile: Option<&mut [Option<u32>]>,
    flusher: Option<&dyn Flusher>,
) {
    if let Some(rect) = crop {
        if !rect.vert.contains(&j) {
            return;
        }
    }

    let segments = search_last_by_key(segments, j as i16, |segment| segment.tile_y())
        .map(|end| {
            let result = search_last_by_key(segments, j as i16 - 1, |segment| segment.tile_y());
            let start = match result {
                Ok(i) => i + 1,
                Err(i) => i,
            };

            &segments[start..=end]
        })
        .unwrap_or(&[]);

    PAINTER_WORKBENCH.with(|pair| {
        let (mut painter, mut workbench) =
            RefMut::map_split(pair.borrow_mut(), |pair| (&mut pair.0, &mut pair.1));

        painter.paint_tile_row::<S, L>(
            &mut workbench,
            j,
            segments,
            styles,
            channels,
            clear_color,
            layers_per_tile,
            row,
            crop.clone(),
            flusher,
        );
    });
}

#[allow(clippy::too_many_arguments)]
#[inline]
pub fn for_each_row<L: Layout, S: LayerProps>(
    layout: &mut L,
    buffer: &mut [u8],
    channels: [Channel; 4],
    flusher: Option<&dyn Flusher>,
    layers_per_tile: Option<RefMut<'_, Vec<Option<u32>>>>,
    mut segments: &[PixelSegment],
    clear_color: Color,
    crop: &Option<Rect>,
    styles: &S,
) {
    if let Ok(end) = search_last_by_key(segments, false, |segment| segment.tile_y().is_negative()) {
        segments = &segments[..=end];
    }

    let width_in_tiles = layout.width_in_tiles();
    let row_of_tiles_len = width_in_tiles * layout.slices_per_tile();
    let mut slices = layout.slices(buffer);

    if let Some(mut layers_per_tile) = layers_per_tile {
        slices
            .par_chunks_mut(row_of_tiles_len)
            .zip_eq(layers_per_tile.par_chunks_mut(width_in_tiles))
            .enumerate()
            .for_each(|(j, (row_of_tiles, layers_per_tile))| {
                print_row::<S, L>(
                    segments,
                    channels,
                    clear_color,
                    crop,
                    styles,
                    j,
                    row_of_tiles.chunks_exact_mut(row_of_tiles.len() / width_in_tiles),
                    Some(layers_per_tile),
                    flusher,
                );
            });
    } else {
        slices.par_chunks_mut(row_of_tiles_len).enumerate().for_each(|(j, row_of_tiles)| {
            print_row::<S, L>(
                segments,
                channels,
                clear_color,
                crop,
                styles,
                j,
                row_of_tiles.chunks_exact_mut(row_of_tiles.len() / width_in_tiles),
                None,
                flusher,
            );
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::{collections::HashMap, iter};

    use crate::{
        layout::LinearLayout,
        painter::style::Color,
        point::Point,
        rasterizer::{self, Rasterizer},
        LinesBuilder, Segment, TILE_SIZE,
    };

    const BLACK: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
    const RED: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
    const RED_50: [f32; 4] = [0.5, 0.0, 0.0, 1.0];
    const GREEN: [f32; 4] = [0.0, 1.0, 0.0, 1.0];
    const GREEN_50: [f32; 4] = [0.0, 0.5, 0.0, 1.0];
    const RED_GREEN_50: [f32; 4] = [0.5, 0.5, 0.0, 1.0];
    const BLACK_RGBA: [u8; 4] = [0, 0, 0, 255];
    const RED_RGBA: [u8; 4] = [255, 0, 0, 255];
    const GREEN_RGBA: [u8; 4] = [0, 255, 0, 255];
    const BLUE_RGBA: [u8; 4] = [0, 0, 255, 255];

    const BLACK_TRANSPARENTF: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.5 };

    const REDF: Color = Color { r: 1.0, g: 0.0, b: 0.0, a: 1.0 };
    const GREENF: Color = Color { r: 0.0, g: 1.0, b: 0.0, a: 1.0 };
    const BLUEF: Color = Color { r: 0.0, g: 0.0, b: 1.0, a: 1.0 };
    const BLACKF: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 };
    const WHITEF: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.0 };

    impl LayerProps for HashMap<u32, Style> {
        fn get(&self, layer_id: u32) -> Cow<'_, Props> {
            let style = self.get(&layer_id).unwrap().clone();

            Cow::Owned(Props { fill_rule: FillRule::NonZero, func: Func::Draw(style) })
        }

        fn is_unchanged(&self, _: u32) -> bool {
            false
        }
    }

    impl LayerProps for HashMap<u32, Props> {
        fn get(&self, layer_id: u32) -> Cow<'_, Props> {
            Cow::Owned(self.get(&layer_id).unwrap().clone())
        }

        fn is_unchanged(&self, _: u32) -> bool {
            false
        }
    }

    impl<F> LayerProps for F
    where
        F: Fn(u32) -> Style + Send + Sync,
    {
        fn get(&self, layer_id: u32) -> Cow<'_, Props> {
            let style = self(layer_id);

            Cow::Owned(Props { fill_rule: FillRule::NonZero, func: Func::Draw(style) })
        }

        fn is_unchanged(&self, _: u32) -> bool {
            false
        }
    }

    impl Painter {
        fn colors(&self) -> [[f32; 4]; TILE_SIZE * TILE_SIZE] {
            let mut colors = [[0.0, 0.0, 0.0, 1.0]; TILE_SIZE * TILE_SIZE];

            for (i, (((&c0, &c1), &c2), &alpha)) in self
                .red
                .iter()
                .flat_map(f32x8::as_array)
                .zip(self.green.iter().flat_map(f32x8::as_array))
                .zip(self.blue.iter().flat_map(f32x8::as_array))
                .zip(self.alpha.iter().flat_map(f32x8::as_array))
                .enumerate()
            {
                colors[i] = [c0, c1, c2, alpha];
            }

            colors
        }
    }

    fn line_segments(points: &[(Point, Point)], same_layer: bool) -> Vec<PixelSegment> {
        let mut builder = LinesBuilder::new();

        for (layer, &(p0, p1)) in points.iter().enumerate() {
            let layer = if same_layer { 0 } else { layer };
            builder.push(layer as u32, &Segment::new(p0, p1));
        }

        let lines = builder.build(|_| None);

        let mut rasterizer = Rasterizer::new();
        rasterizer.rasterize(&lines);

        let mut segments: Vec<_> = rasterizer.segments().iter().copied().collect();
        segments.sort_unstable();

        let last_segment =
            rasterizer::search_last_by_key(&segments, false, |segment| segment.is_none())
                .unwrap_or(0);
        segments.truncate(last_segment + 1);
        segments
    }

    fn paint_tile(
        cover_carries: impl IntoIterator<Item = CoverCarry>,
        segments: &[PixelSegment],
        props: &impl LayerProps,
    ) -> [[f32; 4]; TILE_SIZE * TILE_SIZE] {
        let mut painter = Painter::new();
        let mut workbench = LayerWorkbench::new();

        let context = Context {
            tile_x: 0,
            tile_y: 0,
            segments,
            props,
            previous_layers: Cell::new(None),
            clear_color: BLACKF,
        };

        workbench.init(cover_carries);
        workbench.drive_tile_painting(&mut painter, &context);

        painter.colors()
    }

    #[test]
    fn carry_cover() {
        let mut cover_carry = CoverCarry { cover: Cover::default(), layer_id: 0 };
        cover_carry.cover.covers[0].as_mut_array()[1] = 16;
        cover_carry.layer_id = 1;

        let segments =
            line_segments(&[(Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32))], false);

        let mut styles = HashMap::new();

        styles.insert(0, Style { fill: Fill::Solid(GREENF), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(REDF), ..Default::default() });

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

        styles.insert(0, Style { fill: Fill::Solid(GREENF), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(REDF), ..Default::default() });

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

        styles.insert(0, Style { fill: Fill::Solid(REDF), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(BLACK_TRANSPARENTF), ..Default::default() });

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
                    fill: Fill::Solid(GREENF),
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
                    fill: Fill::Solid(REDF),
                    is_clipped: true,
                    ..Default::default()
                }),
            },
        );
        props.insert(
            3,
            Props {
                fill_rule: FillRule::NonZero,
                func: Func::Draw(Style { fill: Fill::Solid(GREENF), ..Default::default() }),
            },
        );

        let mut painter = Painter::new();
        let mut workbench = LayerWorkbench::new();

        let mut context = Context {
            tile_x: 0,
            tile_y: 0,
            segments: &segments,
            props: &props,
            previous_layers: Cell::new(None),
            clear_color: BLACKF,
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

        context.tile_x = 1;
        context.segments = &segments;

        workbench.drive_tile_painting(&mut painter, &context);

        assert_eq!(painter.colors(), [RED; TILE_SIZE * TILE_SIZE]);
    }

    #[test]
    fn f32_to_u8_scaled() {
        fn convert(val: f32) -> u8 {
            let vals: [u8; 4] = to_u32x4(f32x4::splat(val)).into();
            vals[0]
        }

        assert_eq!(convert(-0.001), 0);
        assert_eq!(convert(1.001), 255);

        for i in 0..255 {
            assert_eq!(convert(i as f32 * 255.0f32.recip()), i);
        }
    }

    #[test]
    fn srgb() {
        let premultiplied = [
            // Small values will still result in > 0 in sRGB.
            0.001 * 0.5,
            // Expected to be < 128.
            0.2 * 0.5,
            // Expected to be > 128.
            0.5 * 0.5,
            // Should convert linearly.
            0.5,
        ];
        assert_eq!(to_srgb_bytes(premultiplied), [3, 124, 187, 128]);
    }

    #[test]
    fn flusher() {
        macro_rules! seg {
            ( $j:expr, $i:expr ) => {
                PixelSegment::new(false, $j, $i, 0, 0, 0, 0, 0)
            };
        }

        #[derive(Debug)]
        struct WhiteFlusher;

        impl Flusher for WhiteFlusher {
            fn flush(&self, slice: &mut [u8]) {
                for color in slice {
                    *color = 255u8;
                }
            }
        }

        let size = TILE_SIZE + TILE_SIZE / 2;
        let mut buffer = vec![0u8; size * size * 4];
        let mut buffer_layout = LinearLayout::new(size, size * 4, size);

        let segments = &[seg!(0, 0), seg!(0, 1), seg!(1, 0), seg!(1, 1)];

        for_each_row(
            &mut buffer_layout,
            &mut buffer,
            RGBA,
            Some(&WhiteFlusher),
            None,
            segments,
            WHITEF,
            &None,
            &|_| Style::default(),
        );

        assert!(buffer.iter().all(|&color| color == 255u8));
    }

    #[test]
    fn flush_background() {
        #[derive(Debug)]
        struct WhiteFlusher;

        impl Flusher for WhiteFlusher {
            fn flush(&self, slice: &mut [u8]) {
                for color in slice {
                    *color = 255u8;
                }
            }
        }

        let mut buffer = vec![0u8; TILE_SIZE * TILE_SIZE * 4];
        let mut buffer_layout = LinearLayout::new(TILE_SIZE, TILE_SIZE * 4, TILE_SIZE);

        for_each_row(
            &mut buffer_layout,
            &mut buffer,
            RGBA,
            Some(&WhiteFlusher),
            None,
            &[],
            WHITEF,
            &None,
            &|_| Style::default(),
        );

        assert!(buffer.iter().all(|&color| color == 255u8));
    }

    #[test]
    fn skip_opaque_tiles() {
        let mut buffer = vec![0u8; TILE_SIZE * TILE_SIZE * 3 * 4];

        let mut buffer_layout = LinearLayout::new(TILE_SIZE * 3, TILE_SIZE * 3 * 4, TILE_SIZE);

        let mut segments = vec![];
        for y in 0..TILE_SIZE {
            segments.push(PixelSegment::new(
                false,
                0,
                -1,
                2,
                y as u8,
                TILE_SIZE as u8 - 1,
                0,
                PIXEL_WIDTH as i8,
            ));
        }

        segments.push(PixelSegment::new(
            false,
            0,
            -1,
            0,
            0,
            TILE_SIZE as u8 - 1,
            0,
            PIXEL_WIDTH as i8,
        ));
        segments.push(PixelSegment::new(false, 0, 0, 1, 1, 0, 0, PIXEL_WIDTH as i8));

        for y in 0..TILE_SIZE {
            segments.push(PixelSegment::new(
                false,
                0,
                1,
                2,
                y as u8,
                TILE_SIZE as u8 - 1,
                0,
                -(PIXEL_WIDTH as i8),
            ));
        }

        segments.sort();

        let mut styles = HashMap::new();

        styles.insert(0, Style { fill: Fill::Solid(BLUEF), ..Default::default() });
        styles.insert(1, Style { fill: Fill::Solid(GREENF), ..Default::default() });
        styles.insert(2, Style { fill: Fill::Solid(REDF), ..Default::default() });

        for_each_row(
            &mut buffer_layout,
            &mut buffer,
            RGBA,
            None,
            None,
            &segments,
            BLACKF,
            &None,
            &|layer| styles[&layer].clone(),
        );

        let tiles = buffer_layout.slices(&mut buffer);

        assert_eq!(
            tiles.iter().map(|slice| slice.to_vec()).collect::<Vec<_>>(),
            // First two tiles need to be completely red.
            iter::repeat(vec![RED_RGBA; TILE_SIZE].concat())
                .take(TILE_SIZE)
                .chain(iter::repeat(vec![RED_RGBA; TILE_SIZE].concat()).take(TILE_SIZE))
                .chain(
                    // The last tile contains one blue and one green line.
                    iter::once(vec![BLUE_RGBA; TILE_SIZE].concat())
                        .chain(iter::once(vec![GREEN_RGBA; TILE_SIZE].concat()))
                        // Followed by black lines (clear color).
                        .chain(
                            iter::repeat(vec![BLACK_RGBA; TILE_SIZE].concat()).take(TILE_SIZE - 2)
                        )
                )
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn crop() {
        let mut buffer = vec![0u8; TILE_SIZE * TILE_SIZE * 9 * 4];

        let mut buffer_layout = LinearLayout::new(TILE_SIZE * 3, TILE_SIZE * 3 * 4, TILE_SIZE * 3);

        let mut segments = vec![];
        for j in 0..3 {
            for y in 0..TILE_SIZE {
                segments.push(PixelSegment::new(
                    false,
                    j,
                    0,
                    0,
                    y as u8,
                    TILE_SIZE as u8 - 1,
                    0,
                    PIXEL_WIDTH as i8,
                ));
            }
        }

        segments.sort();

        let mut styles = HashMap::new();

        styles.insert(0, Style { fill: Fill::Solid(BLUEF), ..Default::default() });

        for_each_row(
            &mut buffer_layout,
            &mut buffer,
            RGBA,
            None,
            None,
            &segments,
            REDF,
            &Some(Rect::new(TILE_SIZE..TILE_SIZE * 2 + TILE_SIZE / 2, TILE_SIZE..TILE_SIZE * 2)),
            &|layer| styles[&layer].clone(),
        );

        let tiles = buffer_layout.slices(&mut buffer);

        assert_eq!(
            tiles.iter().map(|slice| slice.to_vec()).collect::<Vec<_>>(),
            // First row of tiles needs to be completely black.
            iter::repeat(vec![0u8; TILE_SIZE * 4])
                .take(TILE_SIZE * 3)
                // Second row begins with a black tile.
                .chain(iter::repeat(vec![0u8; TILE_SIZE * 4]).take(TILE_SIZE))
                .chain(iter::repeat(vec![BLUE_RGBA; TILE_SIZE].concat()).take(TILE_SIZE * 2))
                // Third row of tiles needs to be completely black as well.
                .chain(iter::repeat(vec![0u8; TILE_SIZE * 4]).take(TILE_SIZE * 3))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn tiles_len() {
        let width = TILE_SIZE * 4;
        let width_stride = TILE_SIZE * 5 * 4;
        let height = TILE_SIZE * 8;

        let buffer_layout = LinearLayout::new(width, width_stride, height);

        assert_eq!(buffer_layout.width_in_tiles() * buffer_layout.height_in_tiles(), 32);
    }
}
