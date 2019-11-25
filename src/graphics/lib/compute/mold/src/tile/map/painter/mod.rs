// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(feature = "tracing")]
use fuchsia_trace::duration;

use crate::{
    point::Point,
    segment::Segment,
    tile::{
        map::{LayerNode, Layers},
        Op, Tile, TILE_SIZE,
    },
    PIXEL_SHIFT, PIXEL_WIDTH,
};

pub(crate) mod buffer;
mod byte_fraction;
mod color;
mod segments;

use buffer::{ColorBuffer, PixelFormat};
use byte_fraction::ByteFraction;
use color::Color;
use segments::TileSegments;

#[derive(Debug)]
pub(crate) struct Context<'m, B: ColorBuffer> {
    pub tile: &'m Tile,
    pub index: usize,
    pub width: usize,
    pub height: usize,
    pub layers: &'m Layers<'m>,
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

#[derive(Debug)]
pub struct Painter {
    cells: Vec<Cell>,
    cover_wip: Vec<ByteFraction>,
    cover_acc: Vec<ByteFraction>,
    cover_mask: Vec<ByteFraction>,
    color_wip: Vec<Color>,
    color_acc: Vec<Color>,
    layer_index: usize,
}

impl Painter {
    pub fn new() -> Self {
        Self {
            cells: vec![Cell::default(); (TILE_SIZE + 1) * TILE_SIZE],
            cover_wip: vec![ByteFraction::zero(); TILE_SIZE * TILE_SIZE],
            cover_acc: vec![ByteFraction::zero(); TILE_SIZE * TILE_SIZE],
            cover_mask: vec![ByteFraction::zero(); TILE_SIZE * TILE_SIZE],
            color_wip: vec![color::ZERO; TILE_SIZE * TILE_SIZE],
            color_acc: vec![color::BLACK; TILE_SIZE * TILE_SIZE],
            layer_index: 0,
        }
    }

    fn index(i: usize, j: usize) -> usize {
        i + j * TILE_SIZE
    }

    fn cover_wip_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_wip[i] = ByteFraction::zero();
        }
    }

    fn cell(&self, i: usize, j: usize) -> &Cell {
        &self.cells[i + j * (TILE_SIZE + 1)]
    }

    fn cell_mut(&mut self, i: usize, j: usize) -> &mut Cell {
        &mut self.cells[i + j * (TILE_SIZE + 1)]
    }

    fn cover_line(&mut self, segment: &Segment<i32>) {
        let border = segment.border();

        let i = border.x >> PIXEL_SHIFT;
        let j = border.y >> PIXEL_SHIFT;

        if i < TILE_SIZE as i32 && j >= 0 && j < TILE_SIZE as i32 {
            if i >= 0 {
                let mut cell = self.cell_mut(i as usize + 1, j as usize);

                cell.area += segment.double_signed_area();
                cell.cover += segment.cover();
            } else {
                let mut cell = self.cell_mut(0, j as usize);

                cell.cover += segment.cover();
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

                    self.cover_wip[index] += ByteFraction::from_area(area, fill_rule);
                }
            }
        }
    }

    fn cover_wip(
        &mut self,
        segments: impl Iterator<Item = Segment<i32>>,
        translation: Point<i32>,
        i: usize,
        j: usize,
        fill_rule: FillRule,
    ) {
        let delta = Point::new(
            (translation.x - (i * TILE_SIZE) as i32) * PIXEL_WIDTH,
            (translation.y - (j * TILE_SIZE) as i32) * PIXEL_WIDTH,
        );

        for i in 0..TILE_SIZE * (TILE_SIZE + 1) {
            self.cells[i] = Cell::default();
        }

        for segment in segments {
            self.cover_line(&Segment::new(
                Point::new(segment.p0.x + delta.x, segment.p0.y + delta.y),
                Point::new(segment.p1.x + delta.x, segment.p1.y + delta.y),
            ));
        }
        self.accumulate(fill_rule);
    }

    fn cover_wip_mask(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_wip[i] *= self.cover_mask[i];
        }
    }

    fn cover_acc_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_acc[i] = ByteFraction::zero();
        }
    }

    fn cover_acc_accumulate(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let inverted = ByteFraction::one() - self.cover_acc[i];
            self.cover_acc[i] += inverted * self.cover_wip[i];
        }
    }

    fn cover_mask_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_mask[i] = ByteFraction::zero();
        }
    }

    fn cover_mask_one(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.cover_mask[i] = ByteFraction::one();
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
            self.cover_mask[i] = ByteFraction::one() - self.cover_mask[i];
        }
    }

    fn color_wip_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_wip[i] = color::ZERO;
        }
    }

    fn color_wip_fill_solid(&mut self, color: u32) {
        let [red, green, blue, alpha] = color.to_be_bytes();
        let color = Color {
            red: ByteFraction::new(red),
            green: ByteFraction::new(green),
            blue: ByteFraction::new(blue),
            alpha: ByteFraction::new(alpha),
        };

        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_wip[i] = color;
        }
    }

    fn color_acc_zero(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_acc[i] = color::BLACK;
        }
    }

    fn color_acc_blend_over(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let cover = self.cover_wip[i] * self.color_acc[i].alpha;

            self.color_acc[i].red += cover * self.color_wip[i].red;
            self.color_acc[i].green += cover * self.color_wip[i].green;
            self.color_acc[i].blue += cover * self.color_wip[i].blue;
            self.color_acc[i].alpha -= cover * self.color_wip[i].alpha;
        }
    }

    fn color_acc_blend_add(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let cover_min = self.cover_wip[i].min(self.color_acc[i].alpha);

            self.color_acc[i].red += cover_min * self.color_wip[i].red;
            self.color_acc[i].green += cover_min * self.color_wip[i].green;
            self.color_acc[i].blue += cover_min * self.color_wip[i].blue;
            self.color_acc[i].alpha -= cover_min * self.color_wip[i].alpha;
        }
    }

    fn color_acc_blend_multiply(&mut self) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            self.color_acc[i].red *= self.cover_wip[i] * self.color_wip[i].red;
            self.color_acc[i].green *= self.cover_wip[i] * self.color_wip[i].green;
            self.color_acc[i].blue *= self.cover_wip[i] * self.color_wip[i].blue;
            self.color_acc[i].alpha = self.cover_wip[i]
                * self.color_wip[i].alpha
                * (ByteFraction::one() - self.color_acc[i].alpha);
        }
    }

    fn color_acc_background(&mut self, color: u32) {
        for i in 0..TILE_SIZE * TILE_SIZE {
            let alpha = self.color_acc[i].alpha;
            let [red, green, blue, _] = color.to_be_bytes();

            self.color_acc[i].red += alpha * ByteFraction::new(red);
            self.color_acc[i].green += alpha * ByteFraction::new(green);
            self.color_acc[i].blue += alpha * ByteFraction::new(blue);
        }
    }

    fn next_index_delta(&mut self, tile: &Tile) -> usize {
        tile.layers[self.layer_index..]
            .iter()
            .enumerate()
            .find(|(_, layer)| match layer {
                LayerNode::Layer(..) => true,
                _ => false,
            })
            .map(|(i, _)| i)
            // Skip to end if no `LayerNode::Layer` found.
            .unwrap_or_else(|| tile.layers.len() - self.layer_index)
    }

    fn process_layer<'a, 'b, B: ColorBuffer>(
        &'a mut self,
        context: &'b Context<'_, B>,
    ) -> Option<(TileSegments<'b>, &'b [Op])> {
        let tile = context.tile;

        if let Some(layer) = tile.layers.get(self.layer_index) {
            self.layer_index += 1;
            let next_index_delta = self.next_index_delta(tile);

            let (segments, translation, ops) = match layer {
                LayerNode::Layer(id, translation) => {
                    let layers = &context.layers;
                    if let (Some(segments), Some(ops)) = (layers.segments(id), layers.ops(id)) {
                        (segments, *translation, ops)
                    } else {
                        // Skip Layers that are not present in the Map anymore.
                        self.layer_index += next_index_delta;
                        return self.process_layer(context);
                    }
                }
                _ => panic!(
                    "self.layer_index must not point at a Layer::Segments before \
                     calling Painter::process_layer"
                ),
            };

            let segments = TileSegments::new(tile, segments, self.layer_index, translation);

            self.layer_index += next_index_delta;
            return Some((segments, &ops));
        }

        None
    }

    unsafe fn write_row<B: ColorBuffer>(buffer: &mut B, index: usize, row: &[Color]) {
        match buffer.pixel_format() {
            PixelFormat::RGBA8888 => {
                let mut new_row = [color::ZERO; TILE_SIZE];
                for (i, color) in row.iter().enumerate() {
                    new_row[i] = color.swap_rb();
                }

                buffer.write_color_at(index, &new_row[0..row.len()]);
            }
            PixelFormat::BGRA8888 => buffer.write_color_at(index, row),
            PixelFormat::RGB565 => {
                let mut new_row = [0u16; TILE_SIZE];
                for (i, color) in row.iter().enumerate() {
                    new_row[i] = color.to_rgb565();
                }

                buffer.write_color_at(index, &new_row[0..row.len()]);
            }
        }
    }

    pub(crate) fn execute<B: ColorBuffer>(&mut self, mut context: Context<'_, B>) {
        #[cfg(feature = "tracing")]
        duration!(
            "gfx",
            "Painter::execute",
            "i" => context.tile.i as u64,
            "j" => context.tile.j as u64
        );

        while let Some((segments, ops)) = self.process_layer(&context) {
            for op in ops {
                #[cfg(feature = "tracing")]
                duration!("gfx:mold", "Painter::execute_op", "op" => op.name());
                match op {
                    Op::CoverWipZero => self.cover_wip_zero(),
                    Op::CoverWipNonZero => self.cover_wip(
                        segments.clone(),
                        segments.translation,
                        context.tile.i,
                        context.tile.j,
                        FillRule::NonZero,
                    ),
                    Op::CoverWipEvenOdd => self.cover_wip(
                        segments.clone(),
                        segments.translation,
                        context.tile.i,
                        context.tile.j,
                        FillRule::EvenOdd,
                    ),
                    Op::CoverWipMask => self.cover_wip_mask(),
                    Op::CoverAccZero => self.cover_acc_zero(),
                    Op::CoverAccAccumulate => self.cover_acc_accumulate(),
                    Op::CoverMaskZero => self.cover_mask_zero(),
                    Op::CoverMaskOne => self.cover_mask_one(),
                    Op::CoverMaskCopyFromWip => self.cover_mask_copy_from_wip(),
                    Op::CoverMaskCopyFromAcc => self.cover_mask_copy_from_acc(),
                    Op::CoverMaskInvert => self.cover_mask_invert(),
                    Op::ColorWipZero => self.color_wip_zero(),
                    Op::ColorWipFillSolid(color) => self.color_wip_fill_solid(*color),
                    Op::ColorAccZero => self.color_acc_zero(),
                    Op::ColorAccBlendOver => self.color_acc_blend_over(),
                    Op::ColorAccBlendAdd => self.color_acc_blend_add(),
                    Op::ColorAccBlendMultiply => self.color_acc_blend_multiply(),
                    Op::ColorAccBackground(color) => self.color_acc_background(*color),
                }
            }
        }

        for j in 0..TILE_SIZE {
            let x = context.tile.i * TILE_SIZE;
            let y = context.tile.j * TILE_SIZE + j;

            if y < context.height {
                let buffer_index = x + y * context.buffer.stride();
                let index = j * TILE_SIZE;
                let delta = (context.width - x).min(TILE_SIZE);

                let tile_range = index..index + delta;

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

    use crate::{layer::Layer, path::Path, point::Point, raster::Raster, tile::Map};

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

        painter.cover_wip(raster.segments().iter(), Point::new(0, 0), 0, 0, fill_rule);

        let mut cover = Vec::with_capacity(width * height);

        for j in 0..height {
            for i in 0..width {
                cover.push(painter.cover_wip[i + j * TILE_SIZE].value());
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
        blend_op: Op,
    ) -> Map {
        let mut map = Map::new(3, 3);

        let mut band_vertical = Path::new();
        polygon(&mut band_vertical, &[(1.0, 3.0), (2.0, 3.0), (2.0, 0.0), (1.0, 0.0)]);

        let mut band_horizontal = Path::new();
        polygon(&mut band_horizontal, &[(0.0, 2.0), (3.0, 2.0), (3.0, 1.0), (0.0, 1.0)]);

        map.global(0, vec![Op::ColorAccZero]);
        map.print(
            1,
            Layer::new(
                Raster::new(&band_vertical),
                vec![
                    Op::CoverWipZero,
                    Op::CoverWipNonZero,
                    Op::ColorWipZero,
                    Op::ColorWipFillSolid(color_vertical),
                    blend_op,
                ],
            ),
        );
        map.print(
            2,
            Layer::new(
                Raster::new(&band_horizontal),
                vec![
                    Op::CoverWipZero,
                    Op::CoverWipNonZero,
                    Op::ColorWipZero,
                    Op::ColorWipFillSolid(color_horizontal),
                    blend_op,
                ],
            ),
        );
        map.global(3, vec![Op::ColorAccBackground(color_background)]);

        map
    }

    #[test]
    fn blend_over() {
        assert_eq!(
            draw_bands(0x2200_00FF, 0x0022_00FF, 0x0000_22FF, Op::ColorAccBlendOver)
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
        let mut map = draw_bands(0x2200_00FF, 0x0022_00FF, 0x0000_22FF, Op::ColorAccBlendOver);

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

    #[test]
    fn subtle_opacity_accumulation() {
        assert_eq!(
            draw_bands(0x0000_0001, 0x0000_0001, 0x0000_0001, Op::ColorAccBlendOver)
                .render_to_bitmap(),
            vec![
                0xFF00_0000,
                0xFE00_0000,
                0xFF00_0000,
                0xFE00_0000,
                0xFD00_0000,
                0xFE00_0000,
                0xFF00_0000,
                0xFE00_0000,
                0xFF00_0000,
            ],
        );
    }

    #[derive(Clone)]
    struct DebugBuffer {
        buffer: [u8; 4],
        format: PixelFormat,
    }

    impl DebugBuffer {
        fn new(format: PixelFormat) -> Self {
            Self { buffer: [0; 4], format }
        }

        fn write(&mut self, color: Color) -> [u8; 4] {
            unsafe {
                Painter::write_row(self, 0, &[color]);
            }
            self.buffer
        }
    }

    impl ColorBuffer for DebugBuffer {
        fn pixel_format(&self) -> PixelFormat {
            self.format
        }

        fn stride(&self) -> usize {
            0
        }

        unsafe fn write_at(&mut self, _: usize, mut src: *const u8, len: usize) {
            for i in 0..4.min(len) {
                self.buffer[i] = src.read();
                src = src.add(1);
            }
        }
    }

    #[test]
    fn pixel_formats() {
        let color = Color {
            red: ByteFraction::new(10),
            green: ByteFraction::new(20),
            blue: ByteFraction::new(30),
            alpha: ByteFraction::new(255),
        };

        assert_eq!(DebugBuffer::new(PixelFormat::RGBA8888).write(color), [10, 20, 30, 255]);

        assert_eq!(DebugBuffer::new(PixelFormat::BGRA8888).write(color), [30, 20, 10, 255]);

        assert_eq!(DebugBuffer::new(PixelFormat::RGB565).write(color), [163, 8, 0, 0]);
    }
}
