// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod painter;

use std::{
    cell::RefCell,
    collections::BTreeMap,
    ops::{Index, IndexMut, Range},
    ptr,
};

#[cfg(feature = "tracing")]
use fuchsia_trace::{self, duration};
use rayon::prelude::*;

use crate::{
    clip::Clip,
    layer::{Content, Layer},
    point::Point,
    raster::RasterSegments,
    segment::Segment,
    tile::{Op, Tile, TILE_MASK, TILE_SHIFT},
    PIXEL_WIDTH,
};

use painter::{
    buffer::{ColorBuffer, PixelFormat},
    Context, Painter,
};

thread_local!(static PAINTER: RefCell<Painter> = RefCell::new(Painter::new()));

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum LayerNode {
    Layer(u32, Point<i32>),
    Segments(Point<i32>, Range<usize>),
}

#[derive(Debug, Default)]
pub(crate) struct Tiles {
    tiles: Vec<Tile>,
    width: usize,
    height: usize,
}

impl Tiles {
    pub fn new(width: usize, height: usize) -> Self {
        fn round_up(n: usize) -> usize {
            if n & TILE_MASK as usize == 0 {
                n >> TILE_SHIFT as usize
            } else {
                (n >> TILE_SHIFT as usize) + 1
            }
        }

        let width = round_up(width);
        let height = round_up(height);

        let mut tiles = Vec::with_capacity(width * height);

        for j in 0..height {
            for i in 0..width {
                tiles.push(Tile::new(i, j));
            }
        }

        Self { tiles, width, height }
    }

    pub fn width(&self) -> usize {
        self.width
    }

    pub fn height(&self) -> usize {
        self.height
    }

    pub fn get_mut(&mut self, i: usize, j: usize) -> &mut Tile {
        &mut self.tiles[i + j * self.width]
    }

    pub fn set_clip(&mut self, clip: Option<Clip>) {
        if let Some(clip) = clip {
            let start_i = 0.max(clip.x >> TILE_SHIFT as usize);
            let start_j = 0.max(clip.y >> TILE_SHIFT as usize);

            let end = clip.x + clip.width;
            let mut end_i = self.width.min(end >> TILE_SHIFT as usize);
            if end & TILE_MASK as usize != 0 {
                end_i += 1;
            }

            let end = clip.y + clip.height;
            let mut end_j = self.height.min(end >> TILE_SHIFT as usize);
            if end & TILE_MASK as usize != 0 {
                end_j += 1;
            }

            for j in 0..self.height {
                for i in 0..self.width {
                    self.get_mut(i, j).is_enabled =
                        start_i <= i && i < end_i && start_j <= j && j < end_j;
                }
            }
        } else {
            for tile in &mut self.tiles {
                tile.is_enabled = true;
            }
        }
    }

    fn print_segment(
        &mut self,
        height: usize,
        segment: &Segment<i32>,
        segment_range: Range<usize>,
        content: &Content,
        is_partial: bool,
    ) {
        let p0 = segment.p0;
        let segment = segment.translate(Point::new(
            content.translation().x * PIXEL_WIDTH,
            content.translation().y * PIXEL_WIDTH,
        ));
        let (i, j) = segment.tile();
        let i = i.max(0);

        if 0 <= j && (j as usize) < height {
            content.contour().for_each_tile_from(self, i as usize, j as usize, |tile| {
                if !is_partial || tile.needs_render {
                    tile.push_segment(p0, segment_range.clone());
                }
            });
        }
    }

    fn render_all<B: ColorBuffer>(
        &self,
        tile_width: usize,
        width: usize,
        height: usize,
        layers: &Layers<'_>,
        buffer: B,
    ) {
        self.tiles
            .par_iter()
            .filter(|tile| tile.needs_render && tile.is_enabled)
            .map(|tile| Context {
                tile,
                index: tile.i + tile.j * tile_width,
                width,
                height,
                layers,
                buffer: buffer.clone(),
            })
            .for_each(|context| {
                PAINTER.with(|painter| {
                    painter.borrow_mut().execute(context);
                });
            });
    }
}

impl Index<usize> for Tiles {
    type Output = Tile;

    fn index(&self, index: usize) -> &Self::Output {
        &self.tiles[index]
    }
}

impl IndexMut<usize> for Tiles {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        &mut self.tiles[index]
    }
}

#[derive(Debug)]
pub(crate) struct Layers<'m> {
    layers: &'m mut BTreeMap<u32, Layer>,
}

impl<'m> Layers<'m> {
    pub fn new(layers: &'m mut BTreeMap<u32, Layer>) -> Self {
        Self { layers }
    }

    pub fn segments(&self, id: &u32) -> Option<&RasterSegments> {
        self.layers.get(id).map(|layer| layer.content.segments())
    }

    pub fn ops(&self, id: &u32) -> Option<&[Op]> {
        self.layers.get(id).map(|layer| &layer.ops[..])
    }
}

// This is safe as long as:
//   1. The potential `Rc` in `Layer.content` is not cloned.
//   2. The `Cell` in the `Layer.needs_render` is not mutated.
//
// The interface above is made to ensure these requirements. The reference to the `HashMap` is
// mutable in oder to guarantee unique access without having to move th whole `HashMap` in and out
// of the `Map` through an `Option<HashMap<_, _>>`.
unsafe impl Sync for Layers<'_> {}

/// A map of [tiles].
///
/// [tiles]: crate::tile
#[derive(Debug, Default)]
pub struct Map {
    tiles: Tiles,
    layers: BTreeMap<u32, Layer>,
    width: usize,
    height: usize,
}

impl Map {
    /// Creates a new map `width` pixels wide and `height` pixels high.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::tile::Map;
    /// let map = Map::new(800, 600);
    /// ```
    pub fn new(width: usize, height: usize) -> Self {
        Self { tiles: Tiles::new(width, height), layers: BTreeMap::new(), width, height }
    }

    #[cfg(test)]
    pub(crate) fn tiles_mut(&mut self) -> &mut Tiles {
        &mut self.tiles
    }

    /// Width of the map, in pixels.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::tile::Map;
    /// let map = Map::new(800, 600);
    /// assert_eq!(map.width(), 800);
    /// ```
    pub fn width(&self) -> usize {
        self.width
    }

    /// Height of the map, in pixels.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::tile::Map;
    /// let map = Map::new(800, 600);
    /// assert_eq!(map.height(), 600);
    /// ```
    pub fn height(&self) -> usize {
        self.height
    }

    /// Set the map's clip. Any subsequent calls to [`Map::render`] will only apply to the [tiles']
    /// that touch the pixel-range defined by the `clip`.
    ///
    /// [tiles']: crate::tile
    /// [`Map::render`]: crate::tile::Map::render
    ///
    /// # Examples
    /// ```
    /// # use std::ptr;
    /// # use crate::mold::{Clip, tile::Map};
    /// # let mut map = Map::new(1, 1);
    /// map.set_clip(Some(Clip {
    ///     x: 100,
    ///     y: 100,
    ///     width: 100,
    ///     height: 100,
    /// }));
    ///
    /// // map.render(bitmap); // This would only activate tiles within the Clip's border.
    /// ```
    pub fn set_clip(&mut self, clip: Option<Clip>) {
        self.tiles.set_clip(clip);
    }

    /// During rendering, execute `ops` batch on *all* tiles.
    ///
    /// # Ordering
    ///
    /// `id`s specify the order in which the batches will be executed, with lower ids being executed
    /// first.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::tile::{Map, Op};
    /// let mut map = Map::new(800, 600);
    /// map.global(0, vec![
    ///     Op::CoverWipZero,
    /// ]);
    /// ```
    pub fn global(&mut self, id: u32, ops: Vec<Op>) {
        self.print(id, Layer::maxed(ops));
    }

    fn touch_tiles(&mut self, content: &Content) {
        content.contour().for_each_tile(&mut self.tiles, |tile| tile.needs_render = true);
    }

    /// During rendering, execute `ops` batch on tiles touched by `layer`.
    ///
    /// # Ordering
    ///
    /// `id`s specify the order in which the batches will be executed, with lower ids being executed
    /// first. It will also blend lower ids on top of higher ones.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Layer, Raster, tile::{Map, Op}};
    /// let mut map = Map::new(800, 600);
    /// map.print(0, Layer::new(Raster::empty(), vec![
    ///     Op::CoverWipZero,
    /// ]));
    /// ```
    pub fn print(&mut self, id: u32, layer: Layer) {
        if !layer.ops.is_empty() && self.layers.get(&id) != Some(&layer) {
            if let Some(old_layer) = self.layers.get(&id) {
                let content = old_layer.content.clone();
                self.touch_tiles(&content);
            }

            self.layers.insert(id, layer.clone());
            self.touch_tiles(&layer.content);
        }
    }

    /// Remove all rendering operations on `id`.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::tile::{Map, Op};
    /// let mut map = Map::new(800, 600);
    /// map.global(0, vec![
    ///     Op::CoverWipZero,
    /// ]);
    /// // render one frame
    /// map.remove(0);
    /// // next frame id == 0 will be skipped
    /// ```
    pub fn remove(&mut self, id: u32) {
        if let Some(layer) = self.layers.get(&id) {
            let content = layer.content.clone();
            self.touch_tiles(&content);
            self.layers.remove(&id);
        }
    }

    fn print_changes(tiles: &mut Tiles, height: usize, id: u32, layer: &Layer) {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Map::print_changes");
        let content = layer.content.clone();
        content.contour().for_each_tile(tiles, |tile| {
            if !layer.is_partial.get() || tile.needs_render {
                tile.new_layer(id, content.translation());
            }
        });

        let mut segments = content.segments().iter();
        let mut prev_index = segments.index();

        while let Some(segment) = segments.next() {
            tiles.print_segment(
                height,
                &segment,
                prev_index..segments.index(),
                &content,
                layer.is_partial.get(),
            );
            prev_index = segments.index();
        }
    }

    fn reprint_all(&mut self) {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Map::reprint_all");
        for layer in self.layers.values() {
            if layer.needs_render.get() {
                let content = layer.content.clone();
                content.contour().for_each_tile(&mut self.tiles, |tile| tile.needs_render = true);
            }
        }

        for tile in &mut self.tiles.tiles {
            if tile.needs_render {
                for node in &tile.layers {
                    if let LayerNode::Layer(id, _) = node {
                        if let Some(layer) = self.layers.get(id) {
                            if !layer.needs_render.get() {
                                layer.is_partial.set(true);
                            }
                        }
                    }
                }

                tile.needs_render = true;
                tile.reset();
            }
        }

        for (id, layer) in &self.layers {
            if layer.needs_render.get() || layer.is_partial.get() {
                Self::print_changes(&mut self.tiles, self.height, *id, layer);

                layer.needs_render.set(false);
                layer.is_partial.set(false);
            }
        }
    }

    /// Render map into `buffer`.
    ///
    /// # Examples
    /// ```
    /// # use std::ptr;
    /// # use crate::mold::{ColorBuffer, PixelFormat, tile::{Map, Op}};
    /// #[derive(Clone, Debug)]
    /// struct BitMap {
    ///     buffer: *mut u8,
    /// }
    ///
    /// unsafe impl Send for BitMap {}
    /// unsafe impl Sync for BitMap {}
    ///
    /// impl ColorBuffer for BitMap {
    ///     // ...
    /// #     fn pixel_format(&self) -> PixelFormat {
    /// #         PixelFormat::RGBA8888
    /// #     }
    /// #
    /// #     fn stride(&self) -> usize {
    /// #         1
    /// #     }
    /// #
    /// #     unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
    /// #         let dst = self.buffer.add(offset);
    /// #         ptr::copy_nonoverlapping(src, dst, len);
    /// #     }
    /// }
    ///
    /// let mut buffer = [0u8; 4];
    /// let mut bitmap = BitMap { buffer: buffer.as_mut_ptr() };
    /// const WHITE: u32 = 0xFFFF_FFFF;
    ///
    /// let mut map = Map::new(1, 1);
    /// map.global(0, vec![
    ///     Op::ColorAccBackground(WHITE),
    /// ]);
    /// map.render(bitmap);
    ///
    /// assert_eq!(u32::from_be_bytes(buffer), WHITE);
    /// ```
    pub fn render<B: ColorBuffer>(&mut self, buffer: B) {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Map::render");
        self.reprint_all();

        self.tiles.render_all(
            self.tiles.width,
            self.width,
            self.height,
            &Layers::new(&mut self.layers),
            buffer,
        );

        for j in 0..self.tiles.height {
            for i in 0..self.tiles.width {
                let tiles_width = self.tiles.width;
                self.tiles[i + j * tiles_width].needs_render = false;
            }
        }
    }

    /// Render map into a `Vec`-backed bitmap.
    ///
    /// # Examples
    /// ```
    /// # use std::ptr;
    /// # use crate::mold::{tile::{Map, Op}};
    /// const WHITE: u32 = 0xFFFF_FFFF;
    ///
    /// let mut map = Map::new(1, 1);
    /// map.global(0, vec![
    ///     Op::ColorAccBackground(WHITE),
    /// ]);
    ///
    /// assert_eq!(map.render_to_bitmap(), vec![WHITE]);
    /// ```
    pub fn render_to_bitmap(&mut self) -> Vec<u32> {
        let mut bitmap = vec![0u32; self.width * self.height];

        self.render(BitMap::new(bitmap.as_mut_ptr() as *mut u8, self.width));

        bitmap
    }

    /// Resets map by clearing all the operations previously submitted.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Layer, Raster, tile::{Map, Op}};
    /// let mut map = Map::new(800, 600);
    /// map.print(0, Layer::new(Raster::empty(), vec![
    ///     Op::CoverWipZero,
    /// ]));
    /// map.reset(); // previous print removed
    /// ```
    pub fn reset(&mut self) {
        self.tiles.tiles.iter_mut().for_each(Tile::reset);
    }
}

#[derive(Clone, Debug)]
struct BitMap {
    buffer: *mut u8,
    stride: usize,
}

impl BitMap {
    pub fn new(buffer: *mut u8, stride: usize) -> Self {
        Self { buffer, stride }
    }
}

unsafe impl Send for BitMap {}
unsafe impl Sync for BitMap {}

impl ColorBuffer for BitMap {
    fn pixel_format(&self) -> PixelFormat {
        PixelFormat::RGBA8888
    }

    fn stride(&self) -> usize {
        self.stride
    }

    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
        let dst = self.buffer.add(offset);
        ptr::copy_nonoverlapping(src, dst, len);
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use crate::{tile::TILE_SIZE, Path, Point, Raster};

    const HALF: usize = TILE_SIZE / 2;

    pub(crate) fn polygon(path: &mut Path, points: &[(f32, f32)]) {
        for window in points.windows(2) {
            path.line(
                Point::new(TILE_SIZE as f32 * window[0].0, TILE_SIZE as f32 * window[0].1),
                Point::new(TILE_SIZE as f32 * window[1].0, TILE_SIZE as f32 * window[1].1),
            );
        }

        if let (Some(first), Some(last)) = (points.first(), points.last()) {
            path.line(
                Point::new(TILE_SIZE as f32 * last.0, TILE_SIZE as f32 * last.1),
                Point::new(TILE_SIZE as f32 * first.0, TILE_SIZE as f32 * first.1),
            );
        }
    }

    fn print(map: &mut Map, id: u32, raster: Raster) {
        map.print(id, Layer::new(raster, vec![Op::CoverWipNonZero]));
    }

    fn segments(raster: &Raster, range: Range<usize>) -> LayerNode {
        let mut segments = raster.segments().iter();
        let mut i = 0;

        let mut point = None;
        let mut start = None;
        let mut end = None;

        loop {
            let index = segments.index();

            if let Some(current_point) = segments.next().map(|segment| segment.p0) {
                if i == range.start {
                    start = Some(index);
                    point = Some(current_point);
                }

                if i == range.end {
                    end = Some(index);
                }

                i += 1;
            } else {
                break;
            }
        }

        if i == range.end {
            end = Some(segments.index());
        }

        LayerNode::Segments(point.unwrap(), start.unwrap()..end.unwrap())
    }

    #[test]
    fn tile_segments_and_rasters() {
        let mut map = Map::new(TILE_SIZE * 3, TILE_SIZE * 3);

        let mut path = Path::new();
        polygon(&mut path, &[(0.5, 0.5), (0.5, 1.5), (1.5, 1.5), (1.5, 0.5)]);

        let raster = Raster::new(&path);

        print(&mut map, 0, raster.clone());

        let mut translated = Raster::new(&path);
        translated.set_translation(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));

        print(&mut map, 1, translated.clone());
        map.reprint_all();

        assert_eq!(
            map.tiles[0].layers,
            vec![LayerNode::Layer(0, Point::new(0, 0)), segments(&raster, 0..HALF)],
        );
        assert_eq!(
            map.tiles[1].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                segments(&raster, 0..HALF),
                segments(&raster, HALF * 3..HALF * 4)
            ]
        );
        assert_eq!(map.tiles[2].layers, vec![]);
        assert_eq!(
            map.tiles[3].layers,
            vec![LayerNode::Layer(0, Point::new(0, 0)), segments(&raster, HALF..HALF * 2)]
        );
        assert_eq!(
            map.tiles[4].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                segments(&raster, HALF..HALF * 3),
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                segments(&translated, 0..HALF),
            ]
        );
        assert_eq!(
            map.tiles[5].layers,
            vec![
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                segments(&translated, 0..HALF),
                segments(&translated, HALF * 3..HALF * 4),
            ]
        );
        assert_eq!(map.tiles[6].layers, vec![]);
        assert_eq!(
            map.tiles[7].layers,
            vec![
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                segments(&translated, HALF..HALF * 2)
            ]
        );
        assert_eq!(
            map.tiles[8].layers,
            vec![
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                segments(&translated, HALF..HALF * 3)
            ]
        );
    }

    #[test]
    fn negative_rasters() {
        let mut map = Map::new(TILE_SIZE, TILE_SIZE);

        let mut path = Path::new();
        polygon(&mut path, &[(-0.5, 0.5), (-0.5, 1.5), (0.5, 1.5), (0.5, 0.5)]);

        let raster = Raster::new(&path);

        print(&mut map, 0, raster.clone());

        map.reprint_all();

        assert_eq!(
            map.tiles[0].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                segments(&raster, 0..HALF),
                segments(&raster, HALF * 3..HALF * 4),
            ],
        );
    }

    #[test]
    fn reprint() {
        fn need_render(map: &Map) -> Vec<bool> {
            map.tiles.tiles.iter().map(|tile| tile.needs_render).collect()
        }

        let mut map = Map::new(TILE_SIZE * 4, TILE_SIZE * 4);

        let mut path = Path::new();
        polygon(&mut path, &[(0.5, 0.5), (0.5, 1.5), (1.5, 1.5), (1.5, 0.5)]);

        let mut raster = Raster::new(&path);

        map.global(0, vec![Op::ColorAccZero]);

        print(&mut map, 1, raster.clone());

        assert_eq!(need_render(&map), vec![true; 16]);

        map.render_to_bitmap();

        assert_eq!(need_render(&map), vec![false; 16]);

        raster.set_translation(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));

        print(&mut map, 1, raster.clone());

        map.reprint_all();

        assert_eq!(
            need_render(&map),
            vec![
                true, true, false, false, true, true, true, false, false, true, true, false, false,
                false, false, false,
            ],
        );

        assert_eq!(map.tiles[0].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);

        assert_eq!(
            map.tiles[5].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                segments(&raster, 0..HALF),
            ],
        );
    }

    #[test]
    fn stride() {
        const WIDTH: usize = TILE_SIZE * 4;
        const HEIGHT: usize = TILE_SIZE * 4;
        let mut map = Map::new(WIDTH, HEIGHT);

        let mut path = Path::new();
        polygon(&mut path, &[(0.5, 0.5), (0.5, 1.5), (1.5, 1.5), (1.5, 0.5)]);

        let raster = Raster::new(&path);

        map.global(0, vec![Op::ColorAccZero]);
        map.print(1, Layer::new(raster.clone(), vec![Op::CoverWipNonZero]));

        // Multiple of 8.
        const STRIDE: usize = (WIDTH + 7) & !7;

        let mut bitmap = vec![0u32; STRIDE * HEIGHT];
        map.render(BitMap::new(bitmap.as_mut_ptr() as *mut u8, STRIDE));
    }

    #[test]
    fn same_raster_multiple_layers() {
        let mut map = Map::new(1, 1);

        let path = Path::new();
        let raster = Raster::new(&path);

        print(&mut map, 0, raster.clone());
        print(&mut map, 1, raster.clone());

        map.render_to_bitmap();
    }

    #[test]
    fn replace_remove_layer() {
        let mut map = Map::new(TILE_SIZE * 2, TILE_SIZE);

        let mut path = Path::new();
        polygon(&mut path, &[(0.0, 0.0), (0.0, 0.5), (0.5, 0.5), (0.5, 0.0)]);

        let raster = Raster::new(&path);

        map.global(0, vec![Op::ColorAccZero]);

        print(&mut map, 1, raster.clone());

        map.reprint_all();

        assert_eq!(
            map.tiles[0].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                LayerNode::Layer(1, Point::new(0, 0)),
                segments(&raster, 0..TILE_SIZE),
            ],
        );

        assert_eq!(map.tiles[1].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);

        let mut path = Path::new();
        polygon(&mut path, &[(1.0, 0.0), (1.0, 0.5), (1.5, 0.5), (1.5, 0.0)]);

        let raster = Raster::new(&path);

        // Replace.
        print(&mut map, 1, raster.clone());

        map.reprint_all();

        assert_eq!(map.tiles[0].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);

        assert_eq!(
            map.tiles[1].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                LayerNode::Layer(1, Point::new(0, 0)),
                segments(&raster, 0..TILE_SIZE),
            ],
        );

        // Remove.
        map.remove(1);

        map.reprint_all();

        assert_eq!(map.tiles[0].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);

        assert_eq!(map.tiles[1].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);
    }

    fn tiles_empty(map: &Map) -> Vec<bool> {
        map.tiles.tiles.iter().map(|tile| tile.layers.is_empty()).collect()
    }

    #[test]
    fn clips() {
        let mut map = Map::new(TILE_SIZE * 3, TILE_SIZE * 2);

        assert_eq!(tiles_empty(&map), vec![true, true, true, true, true, true]);

        map.global(0, vec![Op::ColorAccZero]);
        map.reprint_all();
        assert_eq!(tiles_empty(&map), vec![false, false, false, false, false, false]);

        map.remove(0);

        map.set_clip(Some(Clip {
            x: TILE_SIZE / 2,
            y: TILE_SIZE / 2,
            width: TILE_SIZE,
            height: TILE_SIZE,
        }));
        map.global(0, vec![Op::ColorAccZero]);
        map.reprint_all();
        assert_eq!(tiles_empty(&map), vec![false, false, true, false, false, true]);

        map.set_clip(Some(Clip { x: TILE_SIZE / 2, y: TILE_SIZE / 2, width: 1, height: 1 }));
        map.global(0, vec![Op::ColorAccZero]);
        map.reprint_all();
        assert_eq!(tiles_empty(&map), vec![false, true, true, true, true, true]);

        map.set_clip(Some(Clip {
            x: TILE_SIZE * 3 / 2,
            y: TILE_SIZE * 3 / 2,
            width: 1,
            height: 1,
        }));
        map.global(0, vec![Op::ColorAccZero]);
        map.reprint_all();
        assert_eq!(tiles_empty(&map), vec![true, true, true, true, false, true]);
    }
}
