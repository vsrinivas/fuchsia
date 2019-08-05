// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cmp, collections::HashMap, mem};

use crate::{
    edge::Edge,
    point::Point,
    tile::{LayerNode, Tile, TileOp, TILE_SIZE},
    PIXEL_SHIFT, PIXEL_WIDTH,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PixelFormat {
    RGBA8888,
    BGRA8888,
    RGB565,
}

pub trait ColorBuffer: Clone + Send + Sync {
    fn pixel_format() -> PixelFormat;
    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize);

    unsafe fn write_color_at<C: Copy + Sized>(&mut self, offset: usize, src: &[C]) {
        let size = mem::size_of::<C>();
        self.write_at(offset * size, src.as_ptr() as *const u8, src.len() * size);
    }
}

#[derive(Debug)]
pub(crate) struct Context<'m, B: ColorBuffer> {
    pub tile: &'m Tile,
    pub width: usize,
    pub height: usize,
    pub edges: &'m HashMap<u32, &'m [Edge<i32>]>,
    pub ops: &'m HashMap<u32, &'m [TileOp]>,
    pub buffer: B,
}

#[derive(Clone, Copy, Debug)]
pub enum FillRule {
    NonZero,
    EvenOdd,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Cell {
    pub area: i16,
    pub cover: i8,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Color {
    pub red: u8,
    pub green: u8,
    pub blue: u8,
    pub alpha: u8,
}

const ZERO: Color = Color { red: 0, green: 0, blue: 0, alpha: 0 };

const BLACK: Color = Color { red: 0, green: 0, blue: 0, alpha: 255 };

#[derive(Debug)]
pub struct Painter {
    cells: Vec<Cell>,
    cover_wip: Vec<u8>,
    cover_acc: Vec<u8>,
    cover_mask: Vec<u8>,
    color_wip: Vec<Color>,
    color_acc: Vec<Color>,
    layer_index: usize,
}

#[derive(Clone, Debug)]
struct Edges<'t> {
    tile: &'t Tile,
    edges: &'t [Edge<i32>],
    index: usize,
    inner_index: Option<usize>,
}

impl<'t> Iterator for Edges<'t> {
    type Item = Edge<i32>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(LayerNode::Edges(range)) = self.tile.layers.get(self.index) {
            let inner_index = self.inner_index.unwrap_or(range.start);

            if inner_index < range.end {
                let edge = Some(self.edges[inner_index].clone());
                self.inner_index = Some(inner_index + 1);
                return edge;
            } else {
                self.index += 1;
                self.inner_index = None;

                return self.next();
            }
        }

        None
    }
}

impl Painter {
    pub fn new() -> Self {
        Self {
            cells: vec![Cell::default(); (TILE_SIZE + 1) * TILE_SIZE],
            cover_wip: vec![0; TILE_SIZE * TILE_SIZE],
            cover_acc: vec![0; TILE_SIZE * TILE_SIZE],
            cover_mask: vec![0; TILE_SIZE * TILE_SIZE],
            color_wip: vec![ZERO; TILE_SIZE * TILE_SIZE],
            color_acc: vec![BLACK; TILE_SIZE * TILE_SIZE],
            layer_index: 0,
        }
    }

    fn index(i: usize, j: usize) -> usize {
        i + j * TILE_SIZE
    }

    fn add(a: u8, b: u8) -> u8 {
        let sum = u16::from(a) + u16::from(b);

        if sum > 255 {
            255
        } else {
            sum as u8
        }
    }

    fn sub(a: u8, b: u8) -> u8 {
        let difference = i16::from(a) - i16::from(b);

        if difference < 0 {
            0
        } else {
            difference as u8
        }
    }

    fn mul(a: u8, b: u8) -> u8 {
        let product = u16::from(a) * u16::from(b);
        (product / 255) as u8
    }

    fn cover_wip_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_wip[i] = 0;
        }
    }

    fn cell(&self, i: usize, j: usize) -> &Cell {
        &self.cells[i + j * (TILE_SIZE + 1)]
    }

    fn cell_mut(&mut self, i: usize, j: usize) -> &mut Cell {
        &mut self.cells[i + j * (TILE_SIZE + 1)]
    }

    fn cover_line(&mut self, edge: &Edge<i32>) {
        let border = edge.border();

        let i = border.x >> PIXEL_SHIFT;
        let j = border.y >> PIXEL_SHIFT;

        if i < TILE_SIZE as i32 && j >= 0 && j < TILE_SIZE as i32 {
            if i >= 0 {
                let mut cell = self.cell_mut(i as usize + 1, j as usize);

                cell.area += edge.double_signed_area();
                cell.cover += edge.cover();
            } else {
                let mut cell = self.cell_mut(0, j as usize);

                cell.cover += edge.cover();
            }
        }
    }

    fn get_area(mut full_area: i32, fill_rule: FillRule) -> u8 {
        match fill_rule {
            FillRule::NonZero => {
                if full_area < 0 {
                    Self::get_area(-full_area, fill_rule)
                } else if full_area >= 256 {
                    255
                } else {
                    full_area as u8
                }
            }
            FillRule::EvenOdd => {
                let mut number = full_area / 256;

                if full_area < 0 {
                    full_area -= 1;
                    number -= 1;
                }

                let capped = (full_area % 256 + 256) % 256;

                let area = if number % 2 == 0 { capped } else { 255 - capped };
                area as u8
            }
        }
    }

    fn accumulate(&mut self, fill_rule: FillRule) {
        for j in 0..TILE_SIZE {
            let mut cover = 0;

            for i in 0..=TILE_SIZE {
                let cell = self.cell(i, j);

                let old_cover = cover;
                cover += cell.cover;

                if i != 0 {
                    let area = PIXEL_WIDTH * i32::from(old_cover) + i32::from(cell.area) / 2;
                    let index = Self::index(i - 1, j);

                    let area =
                        self.cover_wip[index].saturating_add(Self::get_area(area, fill_rule));
                    self.cover_wip[index] = area;
                }
            }
        }
    }

    fn cover_wip(
        &mut self,
        edges: impl Iterator<Item = Edge<i32>> + Clone,
        tile_i: usize,
        tile_j: usize,
        fill_rule: FillRule,
    ) {
        for i in 0..TILE_SIZE * (TILE_SIZE + 1) {
            self.cells[i] = Cell::default();
        }

        for edge in edges {
            let edge = Edge::new(
                Point::new(
                    edge.p0.x - (tile_i * TILE_SIZE) as i32 * PIXEL_WIDTH,
                    edge.p0.y - (tile_j * TILE_SIZE) as i32 * PIXEL_WIDTH,
                ),
                Point::new(
                    edge.p1.x - (tile_i * TILE_SIZE) as i32 * PIXEL_WIDTH,
                    edge.p1.y - (tile_j * TILE_SIZE) as i32 * PIXEL_WIDTH,
                ),
            );
            self.cover_line(&edge);
        }
        self.accumulate(fill_rule);
    }

    fn cover_wip_mask(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_wip[i] = Self::mul(self.cover_wip[i], self.cover_mask[i]);
        }
    }

    fn cover_acc_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_acc[i] = 0;
        }
    }

    fn cover_acc_accumulate(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let contrib = Self::mul(Self::sub(255, self.cover_acc[i]), self.cover_wip[i]);
            self.cover_acc[i] = Self::add(self.cover_acc[i], contrib);
        }
    }

    fn cover_mask_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_mask[i] = 0;
        }
    }

    fn cover_mask_one(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_mask[i] = 255;
        }
    }

    fn cover_mask_copy_from_wip(&mut self) {
        self.cover_mask.copy_from_slice(&self.cover_wip);
    }

    fn cover_mask_copy_from_acc(&mut self) {
        self.cover_mask.copy_from_slice(&self.cover_acc);
    }

    fn cover_mask_invert(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_mask[i] = Self::sub(255, self.cover_mask[i]);
        }
    }

    fn color_wip_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_wip[i] = ZERO;
        }
    }

    fn color_wip_fill_solid(&mut self, color: u32) {
        let [red, green, blue, alpha] = color.to_be_bytes();
        let color = Color { red, green, blue, alpha };

        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_wip[i] = color;
        }
    }

    fn color_acc_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_acc[i] = BLACK;
        }
    }

    fn color_acc_blend_over(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let wip = self.color_wip[i];
            let acc = self.color_acc[i];
            let cover = Self::mul(self.cover_wip[i], acc.alpha);

            let red = Self::add(Self::mul(cover, wip.red), acc.red);
            let green = Self::add(Self::mul(cover, wip.green), acc.green);
            let blue = Self::add(Self::mul(cover, wip.blue), acc.blue);
            let alpha = Self::sub(acc.alpha, Self::mul(cover, wip.alpha));

            self.color_acc[i] = Color { red, green, blue, alpha };
        }
    }

    fn color_acc_blend_add(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let wip = self.color_wip[i];
            let acc = self.color_acc[i];
            let cover_min = cmp::min(self.cover_wip[i], acc.alpha);

            let red = Self::add(Self::mul(cover_min, wip.red), acc.red);
            let green = Self::add(Self::mul(cover_min, wip.green), acc.green);
            let blue = Self::add(Self::mul(cover_min, wip.blue), acc.blue);
            let alpha = Self::sub(acc.alpha, Self::mul(cover_min, wip.alpha));

            self.color_acc[i] = Color { red, green, blue, alpha };
        }
    }

    fn color_acc_blend_multiply(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let wip = self.color_wip[i];
            let acc = self.color_acc[i];

            let red = Self::mul(Self::mul(self.cover_wip[i], wip.red), acc.red);
            let green = Self::mul(Self::mul(self.cover_wip[i], wip.green), acc.green);
            let blue = Self::mul(Self::mul(self.cover_wip[i], wip.blue), acc.blue);
            let alpha =
                Self::mul(Self::mul(self.cover_wip[i], wip.alpha), Self::sub(255, acc.alpha));

            self.color_acc[i] = Color { red, green, blue, alpha };
        }
    }

    fn color_acc_background(&mut self, color: u32) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let acc = self.color_acc[i];
            let [red, green, blue, _] = color.to_be_bytes();

            let red = Self::add(Self::mul(acc.alpha, red), acc.red);
            let green = Self::add(Self::mul(acc.alpha, green), acc.green);
            let blue = Self::add(Self::mul(acc.alpha, blue), acc.blue);

            self.color_acc[i] = Color { red, green, blue, alpha: acc.alpha };
        }
    }

    fn process_layer<'a, 'b, B: ColorBuffer>(
        &'a mut self,
        context: &'b Context<B>,
    ) -> Option<(Edges<'b>, &'b [TileOp])> {
        let tile = &context.tile;

        if let Some(layer) = tile.layers.get(self.layer_index) {
            self.layer_index += 1;

            let next_index = tile.layers[self.layer_index..]
                .iter()
                .enumerate()
                .find(|(_, layer)| match layer {
                    LayerNode::Layer(_) => true,
                    _ => false,
                })
                .map(|(i, _)| i)
                .unwrap_or_else(|| tile.layers.len() - self.layer_index);

            let (edges, ops) = match layer {
                LayerNode::Layer(id) => {
                    if let (Some(edges), Some(ops)) = (context.edges.get(id), context.ops.get(id)) {
                        (edges, ops)
                    } else {
                        // Skip Layers that are not present in the Map anymore.
                        self.layer_index += next_index;
                        return self.process_layer(context);
                    }
                }
                _ => panic!(
                    "self.layer_index must not point at a Layer::Edges before \
                     Painter::process_layer"
                ),
            };

            let edges = Edges { tile, edges, index: self.layer_index, inner_index: None };

            self.layer_index += next_index;
            return Some((edges, &ops));
        }

        None
    }

    unsafe fn write_row<B: ColorBuffer>(buffer: &mut B, index: usize, row: &[Color]) {
        match B::pixel_format() {
            PixelFormat::RGBA8888 => buffer.write_color_at(index, row),
            PixelFormat::BGRA8888 => {
                let mut new_row = [ZERO; TILE_SIZE];
                for (i, color) in row.iter().enumerate() {
                    new_row[i] = Color {
                        red: color.blue,
                        green: color.green,
                        blue: color.red,
                        alpha: color.alpha,
                    }
                }

                buffer.write_color_at(index, &new_row[0..row.len()]);
            }
            PixelFormat::RGB565 => {
                let mut new_row = [0u16; TILE_SIZE];
                for (i, color) in row.iter().enumerate() {
                    let red = Self::mul(color.alpha, color.red) as u16;
                    let green = Self::mul(color.alpha, color.green) as u16;
                    let blue = Self::mul(color.alpha, color.blue) as u16;

                    let red = ((red >> 3) & 0x1F) << 11;
                    let green = ((green >> 2) & 0x3F) << 5;
                    let blue = (blue >> 3) & 0x1F;

                    new_row[i] = red | green | blue;
                }

                buffer.write_color_at(index, &new_row[0..row.len()]);
            }
        }
    }

    pub(crate) fn execute<B: ColorBuffer>(&mut self, mut context: Context<B>) {
        while let Some((edges, ops)) = self.process_layer(&context) {
            for op in ops {
                match op {
                    TileOp::CoverWipZero => self.cover_wip_zero(),
                    TileOp::CoverWipNonZero => self.cover_wip(
                        edges.clone(),
                        context.tile.tile_i,
                        context.tile.tile_j,
                        FillRule::NonZero,
                    ),
                    TileOp::CoverWipEvenOdd => self.cover_wip(
                        edges.clone(),
                        context.tile.tile_i,
                        context.tile.tile_j,
                        FillRule::EvenOdd,
                    ),
                    TileOp::CoverWipMask => self.cover_wip_mask(),
                    TileOp::CoverAccZero => self.cover_acc_zero(),
                    TileOp::CoverAccAccumulate => self.cover_acc_accumulate(),
                    TileOp::CoverMaskZero => self.cover_mask_zero(),
                    TileOp::CoverMaskOne => self.cover_mask_one(),
                    TileOp::CoverMaskCopyFromWip => self.cover_mask_copy_from_wip(),
                    TileOp::CoverMaskCopyFromAcc => self.cover_mask_copy_from_acc(),
                    TileOp::CoverMaskInvert => self.cover_mask_invert(),
                    TileOp::ColorWipZero => self.color_wip_zero(),
                    TileOp::ColorWipFillSolid(color) => self.color_wip_fill_solid(*color),
                    TileOp::ColorAccZero => self.color_acc_zero(),
                    TileOp::ColorAccBlendOver => self.color_acc_blend_over(),
                    TileOp::ColorAccBlendAdd => self.color_acc_blend_add(),
                    TileOp::ColorAccBlendMultiply => self.color_acc_blend_multiply(),
                    TileOp::ColorAccBackground(color) => self.color_acc_background(*color),
                }
            }
        }

        for tile_j in 0..TILE_SIZE {
            let x = context.tile.tile_i * TILE_SIZE;
            let y = context.tile.tile_j * TILE_SIZE + tile_j;

            if y < context.height {
                let buffer_index = x + y * context.width;
                let tile_index = tile_j * TILE_SIZE;
                let d = cmp::min(context.width - x, TILE_SIZE);

                let tile_range = tile_index..tile_index + d;

                unsafe {
                    Self::write_row(&mut context.buffer, buffer_index, &self.color_acc[tile_range]);
                }
            }
        }

        self.layer_index = 0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{
        path::Path,
        point::Point,
        raster::Raster,
        tile::{Layer, Map},
    };

    fn polygon(path: &mut Path, points: &[(f32, f32)]) {
        for window in points.windows(2) {
            path.line(Point::new(window[0].0, window[0].1), Point::new(window[1].0, window[1].1));
        }

        if let (Some(first), Some(last)) = (points.first(), points.last()) {
            path.line(Point::new(last.0, last.1), Point::new(first.0, first.1));
        }
    }

    fn get_cover(width: usize, height: usize, raster: &Raster, fill_rule: FillRule) -> Vec<u8> {
        let mut painter = Painter::new();

        painter.cover_wip(raster.edges().iter().cloned(), 0, 0, fill_rule);

        let mut cover = Vec::with_capacity(width * height);

        for j in 0..height {
            for i in 0..width {
                cover.push(painter.cover_wip[i + j * TILE_SIZE]);
            }
        }

        cover
    }

    #[test]
    fn triangle() {
        let mut path = Path::new();

        path.line(Point::new(0.0, 0.0), Point::new(2.0, 2.0));
        path.line(Point::new(2.0, 2.0), Point::new(4.0, 0.0));

        assert_eq!(
            get_cover(4, 2, &Raster::new(&path), FillRule::NonZero),
            vec![128, 255, 255, 128, 0, 128, 128, 0],
        );
    }

    #[test]
    fn out_of_bounds() {
        let mut path = Path::new();

        let ex = 2.0f32.sqrt() / 2.0;

        polygon(&mut path, &[(0.5, 0.5 + ex), (0.5 + ex, 0.5), (0.5, 0.5 - ex), (0.5 - ex, 0.5)]);

        assert_eq!(get_cover(1, 1, &Raster::new(&path), FillRule::NonZero), vec![206]);
    }

    #[test]
    fn non_zero() {
        let mut path = Path::new();

        polygon(&mut path, &[(1.0, 3.0), (2.0, 3.0), (2.0, 0.0), (1.0, 0.0)]);

        polygon(&mut path, &[(0.0, 2.0), (3.0, 2.0), (3.0, 1.0), (0.0, 1.0)]);

        assert_eq!(
            get_cover(3, 3, &Raster::new(&path), FillRule::NonZero),
            vec![0, 255, 0, 255, 255, 255, 0, 255, 0],
        );
    }

    #[test]
    fn non_zero_reversed() {
        let mut path = Path::new();

        polygon(&mut path, &[(1.0, 3.0), (1.0, 0.0), (2.0, 0.0), (2.0, 3.0)]);

        polygon(&mut path, &[(0.0, 2.0), (3.0, 2.0), (3.0, 1.0), (0.0, 1.0)]);

        assert_eq!(
            get_cover(3, 3, &Raster::new(&path), FillRule::NonZero),
            vec![0, 255, 0, 255, 0, 255, 0, 255, 0],
        );
    }

    #[test]
    fn even_odd() {
        let mut path = Path::new();

        polygon(&mut path, &[(1.0, 3.0), (2.0, 3.0), (2.0, 0.0), (1.0, 0.0)]);

        polygon(&mut path, &[(0.0, 2.0), (3.0, 2.0), (3.0, 1.0), (0.0, 1.0)]);

        assert_eq!(
            get_cover(3, 3, &Raster::new(&path), FillRule::EvenOdd),
            vec![0, 255, 0, 255, 0, 255, 0, 255, 0],
        );
    }

    fn draw_bands(
        color_vertical: u32,
        color_horizontal: u32,
        color_background: u32,
        blend_op: TileOp,
    ) -> Map {
        let mut map = Map::new(3, 3);

        let mut band_vertical = Path::new();
        polygon(&mut band_vertical, &[(1.0, 3.0), (2.0, 3.0), (2.0, 0.0), (1.0, 0.0)]);

        let mut band_horizontal = Path::new();
        polygon(&mut band_horizontal, &[(0.0, 2.0), (3.0, 2.0), (3.0, 1.0), (0.0, 1.0)]);

        map.global(0, vec![TileOp::ColorAccZero]);
        map.print(
            1,
            Layer {
                raster: Raster::new(&band_vertical),
                ops: vec![
                    TileOp::CoverWipZero,
                    TileOp::CoverWipNonZero,
                    TileOp::ColorWipZero,
                    TileOp::ColorWipFillSolid(color_vertical),
                    blend_op,
                ],
            },
        );
        map.print(
            2,
            Layer {
                raster: Raster::new(&band_horizontal),
                ops: vec![
                    TileOp::CoverWipZero,
                    TileOp::CoverWipNonZero,
                    TileOp::ColorWipZero,
                    TileOp::ColorWipFillSolid(color_horizontal),
                    blend_op,
                ],
            },
        );
        map.global(3, vec![TileOp::ColorAccBackground(color_background)]);

        map
    }

    #[test]
    fn blend_over() {
        assert_eq!(
            draw_bands(0x2200_00FF, 0x0022_00FF, 0x0000_22FF, TileOp::ColorAccBlendOver,)
                .render_to_bitmap(),
            vec![
                0xFF22_0000,
                0x0000_0022,
                0xFF22_0000,
                0x0000_2200,
                0x0000_0022,
                0x0000_2200,
                0xFF22_0000,
                0x0000_0022,
                0xFF22_0000,
            ],
        );
    }

    #[test]
    fn blend_over_then_remove() {
        let mut map = draw_bands(0x2200_00FF, 0x0022_00FF, 0x0000_22FF, TileOp::ColorAccBlendOver);

        assert_eq!(
            map.render_to_bitmap(),
            vec![
                0xFF22_0000,
                0x0000_0022,
                0xFF22_0000,
                0x0000_2200,
                0x0000_0022,
                0x0000_2200,
                0xFF22_0000,
                0x0000_0022,
                0xFF22_0000,
            ],
        );

        map.remove(1);

        assert_eq!(
            map.render_to_bitmap(),
            vec![
                0xFF22_0000,
                0xFF22_0000,
                0xFF22_0000,
                0x0000_2200,
                0x0000_2200,
                0x0000_2200,
                0xFF22_0000,
                0xFF22_0000,
                0xFF22_0000,
            ],
        );
    }
}
