// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    collections::{HashMap, HashSet},
    ops::Range,
    ptr,
};

use rayon::slice::ParallelSliceMut;

use crate::{
    edge::Edge,
    painter::{Context, Painter},
    raster::Raster,
    PIXEL_SHIFT,
};

pub use crate::painter::{ColorBuffer, PixelFormat};

pub(crate) const TILE_SIZE: usize = 32;

thread_local!(static PAINTER: RefCell<Painter> = RefCell::new(Painter::new()));

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TileOp {
    CoverWipZero,
    CoverWipNonZero,
    CoverWipEvenOdd,
    CoverWipMask,
    CoverAccZero,
    CoverAccAccumulate,
    CoverMaskZero,
    CoverMaskOne,
    CoverMaskCopyFromWip,
    CoverMaskCopyFromAcc,
    CoverMaskInvert,
    ColorWipZero,
    ColorWipFillSolid(u32),
    ColorAccZero,
    ColorAccBlendOver,
    ColorAccBlendAdd,
    ColorAccBlendMultiply,
    ColorAccBackground(u32),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum LayerNode {
    Layer(u32),
    Edges(Range<usize>),
}

#[derive(Clone, Debug)]
pub(crate) struct Tile {
    pub tile_i: usize,
    pub tile_j: usize,
    pub layers: Vec<LayerNode>,
    pub needs_render: bool,
}

impl Tile {
    pub fn new(tile_i: usize, tile_j: usize) -> Self {
        Self { tile_i, tile_j, layers: vec![], needs_render: true }
    }

    pub fn new_layer(&mut self, id: u32) {
        self.layers.push(LayerNode::Layer(id));
        self.needs_render = true;
    }

    // TODO: Remove lint exception when [#3307] is solved.
    //       [#3307]: https://github.com/rust-lang/rust-clippy/issues/3307
    #[allow(clippy::range_plus_one)]
    pub fn push_edge(&mut self, edge_index: usize) {
        if let Some(LayerNode::Layer(_)) = self.layers.last() {
            self.layers.push(LayerNode::Edges(edge_index..edge_index + 1));
            return;
        }

        let edges_range = match self.layers.last_mut() {
            Some(LayerNode::Edges(range)) => range,
            _ => panic!("Tile::new_layer should be called before Tile::push_edge"),
        };

        if edges_range.end == edge_index {
            edges_range.end += 1;
        } else {
            self.layers.push(LayerNode::Edges(edge_index..edge_index + 1));
        }
    }

    pub fn reset(&mut self) {
        self.layers.clear();
        self.needs_render = true;
    }
}

fn edge_tile(edge: &Edge<i32>) -> Option<(usize, usize)> {
    let mut p0 = edge.p0;
    p0.x >>= PIXEL_SHIFT;
    p0.y >>= PIXEL_SHIFT;
    let mut p1 = edge.p1;
    p1.x >>= PIXEL_SHIFT;
    p1.y >>= PIXEL_SHIFT;

    let min_x = p0.x.min(p1.x);
    let min_x = min_x.max(0);
    let min_y = p0.y.min(p1.y);

    if min_y >= 0 {
        let i = min_x as usize / TILE_SIZE;
        let j = min_y as usize / TILE_SIZE;

        return Some((i, j));
    }

    None
}

#[derive(Debug)]
pub(crate) struct TileContourBuilder {
    tiles: HashSet<(usize, usize)>,
}

impl TileContourBuilder {
    pub fn new() -> Self {
        Self { tiles: HashSet::new() }
    }

    pub fn maxed() -> TileContour {
        TileContour::Maxed
    }

    pub fn enclose(&mut self, edge: &Edge<i32>) -> Option<(usize, usize)> {
        edge_tile(edge).map(|tile| {
            self.tiles.insert((tile.1, tile.0));
            tile
        })
    }

    pub fn build(self) -> TileContour {
        let mut new_tiles = vec![];
        let mut tiles: Vec<_> = self.tiles.into_iter().collect();
        tiles.sort();
        let mut tiles = tiles.into_iter().peekable();

        while let Some(tile) = tiles.next() {
            let mut next = None;
            loop {
                if let Some(peek) = tiles.peek() {
                    if tile.0 == peek.0 {
                        next = tiles.next();
                        continue;
                    }
                }

                break;
            }

            if let Some(next) = next {
                if tile.0 == next.0 {
                    for i in tile.1..=next.1 {
                        new_tiles.push((tile.0, i));
                    }

                    continue;
                }
            }

            new_tiles.push(tile);
        }

        TileContour::Tiles(new_tiles)
    }
}

#[derive(Debug)]
pub(crate) enum TileContour {
    Tiles(Vec<(usize, usize)>),
    Maxed,
}

impl TileContour {
    pub fn for_each_tile(&self, map: &mut Map, mut f: impl FnMut(&mut Tile)) {
        match self {
            Self::Tiles(tiles) => {
                for &(j, i) in tiles {
                    if i < map.tile_width && j < map.tile_height {
                        f(map.tile_mut(i, j));
                    }
                }
            }
            Self::Maxed => {
                for j in 0..map.tile_height {
                    for i in 0..map.tile_width {
                        f(map.tile_mut(i, j));
                    }
                }
            }
        }
    }

    pub fn for_each_tile_from(
        &self,
        map: &mut Map,
        from_i: usize,
        from_j: usize,
        mut f: impl FnMut(&mut Tile),
    ) {
        match self {
            Self::Tiles(tiles) => {
                if from_i < map.tile_width && from_j < map.tile_height {
                    let start = match tiles.binary_search(&(from_j, from_i)) {
                        Ok(i) | Err(i) => i,
                    };
                    let end = match tiles.binary_search(&(from_j, map.tile_width)) {
                        Ok(i) | Err(i) => i,
                    };

                    for &(j, i) in &tiles[start..end] {
                        f(map.tile_mut(i, j));
                    }
                }
            }
            Self::Maxed => {
                for i in from_i..map.tile_width {
                    f(map.tile_mut(i, from_j));
                }
            }
        }
    }

    pub fn union(&self, other: &Self) -> Self {
        match (self, other) {
            (Self::Tiles(tiles), Self::Tiles(other_tiles)) => {
                let mut tiles: Vec<_> =
                    tiles.iter().cloned().chain(other_tiles.iter().cloned()).collect();
                tiles.sort();

                Self::Tiles(tiles)
            }
            _ => Self::Maxed,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Layer {
    pub raster: Raster,
    pub ops: Vec<TileOp>,
}

#[derive(Debug)]
pub struct Map {
    tiles: Vec<Tile>,
    layers: HashMap<u32, Layer>,
    width: usize,
    height: usize,
    tile_width: usize,
    tile_height: usize,
}

impl Map {
    fn round_up(n: usize) -> usize {
        if n % TILE_SIZE == 0 {
            n / TILE_SIZE
        } else {
            n / TILE_SIZE + 1
        }
    }

    pub fn new(width: usize, height: usize) -> Self {
        let tile_width = Self::round_up(width);
        let tile_height = Self::round_up(height);

        let mut tiles = Vec::with_capacity(tile_width * tile_height);

        for j in 0..tile_height {
            for i in 0..tile_width {
                tiles.push(Tile::new(i, j));
            }
        }

        Self { tiles, layers: HashMap::new(), width, height, tile_width, tile_height }
    }

    fn tile_mut(&mut self, i: usize, j: usize) -> &mut Tile {
        &mut self.tiles[i + j * self.tile_width]
    }

    pub fn global(&mut self, id: u32, ops: Vec<TileOp>) {
        self.print(id, Layer { raster: Raster::maxed(), ops });
    }

    fn print_edge(
        &mut self,
        edge: &Edge<i32>,
        edge_index: usize,
        raster: &Raster,
        is_partial: bool,
    ) {
        if let Some((i, j)) = edge_tile(edge) {
            if j < self.height {
                raster.tile_contour().for_each_tile_from(self, i, j, |tile| {
                    if !is_partial || tile.needs_render {
                        tile.push_edge(edge_index);
                    }
                });
            }
        }
    }

    pub fn print(&mut self, id: u32, layer: Layer) {
        if !layer.ops.is_empty() && self.layers.get(&id) != Some(&layer) {
            self.layers.insert(id, layer.clone());
            layer.raster.tile_contour().for_each_tile(self, |tile| tile.needs_render = true);
        }
    }

    pub fn remove(&mut self, id: u32) {
        if let Some(layer) = self.layers.get(&id) {
            let raster = layer.raster.clone();
            raster.tile_contour().for_each_tile(self, |tile| tile.needs_render = true);
            self.layers.remove(&id);
        }
    }

    fn specialized_print(&mut self, id: u32, is_partial: bool) {
        if let Some(layer) = self.layers.get(&id) {
            let raster = layer.raster.clone();
            raster.tile_contour().for_each_tile(self, |tile| {
                if !is_partial || tile.needs_render {
                    tile.new_layer(id);
                }
            });

            for (i, edge) in raster.edges().iter().enumerate() {
                self.print_edge(&edge, i, &raster, is_partial);
            }
        }
    }

    fn reprint_all(&mut self) {
        let complete_reprints: HashSet<_> = self
            .layers
            .iter()
            .filter_map(|(id, layer)| if layer.raster.new_edges() { Some(*id) } else { None })
            .collect();

        if complete_reprints.is_empty() {
            return;
        }

        let mut partial_reprints = HashSet::new();

        for id in &complete_reprints {
            let raster = self.layers.get(id).unwrap().raster.clone();
            raster.tile_contour().for_each_tile(self, |tile| tile.needs_render = true);
        }

        for tile in &mut self.tiles {
            let mut needs_render = false;

            for node in &tile.layers {
                if let LayerNode::Layer(id) = node {
                    if complete_reprints.contains(id) || !self.layers.contains_key(id) {
                        needs_render = true;
                    }
                }
            }

            if needs_render {
                for node in &tile.layers {
                    if let LayerNode::Layer(id) = node {
                        if !complete_reprints.contains(id) {
                            partial_reprints.insert(*id);
                        }
                    }
                }

                tile.needs_render = true;
                tile.reset();
            }
        }

        let mut reprints: Vec<_> = complete_reprints
            .into_iter()
            .map(|id| (id, false))
            .chain(partial_reprints.into_iter().map(|id| (id, true)))
            .collect();
        reprints.par_sort();

        for (id, is_partial) in reprints {
            self.specialized_print(id, is_partial);
        }
    }

    pub fn render<B: ColorBuffer>(&mut self, buffer: B) {
        self.reprint_all();

        let edges: HashMap<_, _> =
            self.layers.iter().map(|(&id, layer)| (id, layer.raster.edges())).collect();
        let edges: HashMap<_, &[Edge<i32>]> =
            edges.iter().map(|(&id, edges)| (id, &*edges as &[Edge<i32>])).collect();
        let ops: HashMap<_, _> =
            self.layers.iter().map(|(&id, layer)| (id, &layer.ops[..])).collect();

        let tiles = &self.tiles;
        let tile_width = self.tile_width;
        let tile_height = self.tile_height;

        let width = self.width;
        let height = self.height;

        rayon::scope(|s| {
            for j in 0..tile_height {
                for i in 0..tile_width {
                    let tile = &tiles[i + j * tile_width];

                    if tile.needs_render {
                        let context = Context {
                            tile,
                            width,
                            height,
                            edges: &edges,
                            ops: &ops,
                            buffer: buffer.clone(),
                        };

                        s.spawn(move |_| {
                            PAINTER.with(|painter| {
                                painter.borrow_mut().execute(context);
                            });
                        });
                    }
                }
            }
        });

        for j in 0..self.tile_height {
            for i in 0..self.tile_width {
                self.tiles[i + j * self.tile_width].needs_render = false;
            }
        }
    }

    pub fn render_to_bitmap(&mut self) -> Vec<u32> {
        let mut bitmap = vec![0u32; self.width * self.height];

        self.render(BitMap::new(bitmap.as_mut_ptr() as *mut u8, self.width));

        bitmap
    }

    pub fn reset(&mut self) {
        self.tiles.iter_mut().for_each(Tile::reset);
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
mod tests {
    use super::*;

    use crate::{Path, Point};

    const HALF: usize = TILE_SIZE / 2;

    fn map_and_contour() -> (Map, TileContour) {
        let map = Map::new(TILE_SIZE * 5, TILE_SIZE * 3);
        let mut tile_contour_builder = TileContourBuilder::new();

        let edge = Edge::new(
            Point::new(TILE_SIZE as f32 * 3.0, TILE_SIZE as f32 * 3.0),
            Point::new(0.0, 0.0),
        );
        for edge in edge.to_sp_edges().unwrap() {
            tile_contour_builder.enclose(&edge);
        }

        let edge = Edge::new(
            Point::new(TILE_SIZE as f32 * 5.0, TILE_SIZE as f32 * 3.0),
            Point::new(TILE_SIZE as f32 * 2.0, 0.0),
        );
        for edge in edge.to_sp_edges().unwrap() {
            tile_contour_builder.enclose(&edge);
        }

        (map, tile_contour_builder.build())
    }

    #[test]
    fn tile_contour_all_tiles() {
        let (mut map, tile_contour) = map_and_contour();

        let mut tiles = vec![];
        tile_contour.for_each_tile(&mut map, |tile| tiles.push((tile.tile_i, tile.tile_j)));
        assert_eq!(
            tiles,
            vec![(0, 0), (1, 0), (2, 0), (1, 1), (2, 1), (3, 1), (2, 2), (3, 2), (4, 2)]
        );
    }

    #[test]
    fn tile_contour_row_tiles() {
        let (mut map, tile_contour) = map_and_contour();

        let mut tiles = vec![];
        tile_contour
            .for_each_tile_from(&mut map, 2, 0, |tile| tiles.push((tile.tile_i, tile.tile_j)));
        assert_eq!(tiles, vec![(2, 0)]);

        let mut tiles = vec![];
        tile_contour
            .for_each_tile_from(&mut map, 2, 1, |tile| tiles.push((tile.tile_i, tile.tile_j)));
        assert_eq!(tiles, vec![(2, 1), (3, 1)]);

        let mut tiles = vec![];
        tile_contour
            .for_each_tile_from(&mut map, 2, 2, |tile| tiles.push((tile.tile_i, tile.tile_j)));
        assert_eq!(tiles, vec![(2, 2), (3, 2), (4, 2)]);
    }

    fn polygon(path: &mut Path, points: &[(f32, f32)]) {
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

    #[test]
    fn tile_edges_and_rasters() {
        let mut map = Map::new(TILE_SIZE * 3, TILE_SIZE * 3);

        let mut path = Path::new();
        polygon(&mut path, &[(0.5, 0.5), (0.5, 1.5), (1.5, 1.5), (1.5, 0.5)]);

        map.print(0, Layer { raster: Raster::new(&path), ops: vec![TileOp::CoverWipNonZero] });

        let mut translated = Raster::new(&path);
        translated.translate(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));

        map.print(1, Layer { raster: translated, ops: vec![TileOp::CoverWipNonZero] });
        map.reprint_all();

        assert_eq!(map.tiles[0].layers, vec![LayerNode::Layer(0), LayerNode::Edges(0..HALF)],);
        assert_eq!(
            map.tiles[1].layers,
            vec![
                LayerNode::Layer(0),
                LayerNode::Edges(0..HALF),
                LayerNode::Edges(HALF * 3..HALF * 4)
            ]
        );
        assert_eq!(map.tiles[2].layers, vec![]);
        assert_eq!(
            map.tiles[3].layers,
            vec![LayerNode::Layer(0), LayerNode::Edges(HALF..HALF * 2)]
        );
        assert_eq!(
            map.tiles[4].layers,
            vec![
                LayerNode::Layer(0),
                LayerNode::Edges(HALF..HALF * 3),
                LayerNode::Layer(1),
                LayerNode::Edges(0..HALF),
            ]
        );
        assert_eq!(
            map.tiles[5].layers,
            vec![
                LayerNode::Layer(1),
                LayerNode::Edges(0..HALF),
                LayerNode::Edges(HALF * 3..HALF * 4),
            ]
        );
        assert_eq!(map.tiles[6].layers, vec![]);
        assert_eq!(
            map.tiles[7].layers,
            vec![LayerNode::Layer(1), LayerNode::Edges(HALF..HALF * 2)]
        );
        assert_eq!(
            map.tiles[8].layers,
            vec![LayerNode::Layer(1), LayerNode::Edges(HALF..HALF * 3)]
        );
    }

    #[test]
    fn reprint() {
        let mut map = Map::new(TILE_SIZE * 4, TILE_SIZE * 4);

        let mut path = Path::new();
        polygon(&mut path, &[(0.5, 0.5), (0.5, 1.5), (1.5, 1.5), (1.5, 0.5)]);

        let mut raster = Raster::new(&path);

        map.global(0, vec![TileOp::ColorAccZero]);

        map.print(1, Layer { raster: raster.clone(), ops: vec![TileOp::CoverWipNonZero] });

        assert_eq!(
            map.tiles.iter().map(|tile| tile.needs_render).collect::<Vec<_>>(),
            vec![true; 16],
        );

        map.render_to_bitmap();

        assert_eq!(
            map.tiles.iter().map(|tile| tile.needs_render).collect::<Vec<_>>(),
            vec![false; 16],
        );

        raster.translate(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));

        map.reprint_all();

        assert_eq!(
            map.tiles.iter().map(|tile| tile.needs_render).collect::<Vec<_>>(),
            vec![
                true, true, false, false, true, true, true, false, false, true, true, false, false,
                false, false, false,
            ],
        );

        assert_eq!(map.tiles[0].layers, vec![LayerNode::Layer(0)],);

        assert_eq!(
            map.tiles[5].layers,
            vec![LayerNode::Layer(0), LayerNode::Layer(1), LayerNode::Edges(0..HALF)],
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

        map.global(0, vec![TileOp::ColorAccZero]);
        map.print(1, Layer { raster: raster.clone(), ops: vec![TileOp::CoverWipNonZero] });

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

        map.print(
            0,
            Layer {
                raster: raster.clone(),
                ops: vec![TileOp::CoverWipNonZero],
            },
        );
        map.print(
            1,
            Layer {
                raster: raster.clone(),
                ops: vec![TileOp::CoverWipNonZero],
            },
        );

        map.render_to_bitmap();
    }
}
