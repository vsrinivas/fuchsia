// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::{Cell, RefCell},
    collections::{HashMap, HashSet},
    mem,
    ops::Range,
    ptr,
};

#[cfg(feature = "tracing")]
use fuchsia_trace::{self, duration};
use rayon::slice::ParallelSliceMut;

use crate::{
    edge::Edge,
    painter::{Context, Painter},
    point::Point,
    raster::Raster,
    PIXEL_SHIFT, PIXEL_WIDTH,
};

pub use crate::painter::{ColorBuffer, PixelFormat};

pub(crate) const TILE_SIZE: usize = 32;

thread_local!(static PAINTER: RefCell<Painter> = RefCell::new(Painter::new()));

macro_rules! ops {
    ( @filter $slf:expr, [], [ $( $v1:ident )* ],  [ $( $v2:ident )* ] ) => {
        match $slf {
            $( TileOp::$v1 => stringify!($v1), )*
            $( TileOp::$v2(..) => stringify!($v2), )*
        }
    };

    ( @filter $slf:expr, [ $var:ident ( $( $_:tt )* )  $( $tail:tt )* ], $v1:tt, [ $( $v2:tt )* ] ) => {
        ops!(@filter $slf, [$($tail)*], $v1, [$var $($v2)*])
    };

    ( @filter $slf:expr, [ $var:ident $( $tail:tt )* ], [ $( $v1:tt )* ], $v2:tt ) => {
        ops!(@filter $slf, [$($tail)*], [$var $($v1)*], $v2)
    };

    ( @filter $slf:expr, [ $_:tt $( $tail:tt )* ], $v1:tt, $v2:tt ) => {
        ops!(@filter $slf, [$($tail)*], $v1, $v2)
    };

    ( $( $tokens:tt )* ) => {
        #[derive(Clone, Copy, Debug, Eq, PartialEq)]
        pub enum TileOp {
            $( $tokens )*
        }

        impl TileOp {
            #[cfg(feature = "tracing")]
            #[inline]
            pub(crate) fn name(&self) -> &'static str {
                ops!(@filter self, [$( $tokens )*], [], [])
            }
        }
    };
}

ops! {
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
    Layer(u32, Point<i32>),
    Edges(Point<i32>, Range<usize>),
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

    pub fn new_layer(&mut self, id: u32, translation: Point<i32>) {
        self.layers.push(LayerNode::Layer(id, translation));
        self.needs_render = true;
    }

    pub fn push_edge(&mut self, start_point: Point<i32>, edge_range: Range<usize>) {
        if let Some(LayerNode::Layer(..)) = self.layers.last() {
            self.layers.push(LayerNode::Edges(start_point, edge_range));
            return;
        }

        let old_range = match self.layers.last_mut() {
            Some(LayerNode::Edges(_, range)) => range,
            _ => panic!("Tile::new_layer should be called before Tile::push_edge"),
        };

        if old_range.end == edge_range.start {
            old_range.end = edge_range.end;
        } else {
            self.layers.push(LayerNode::Edges(start_point, edge_range));
        }
    }

    pub fn reset(&mut self) {
        self.layers.clear();
        self.needs_render = true;
    }
}

fn edge_tile(edge: &Edge<i32>) -> (i32, i32) {
    let mut p0 = edge.p0;
    p0.x >>= PIXEL_SHIFT;
    p0.y >>= PIXEL_SHIFT;
    let mut p1 = edge.p1;
    p1.x >>= PIXEL_SHIFT;
    p1.y >>= PIXEL_SHIFT;

    let min_x = p0.x.min(p1.x);
    let min_y = p0.y.min(p1.y);

    let mut i = min_x / TILE_SIZE as i32;
    let mut j = min_y / TILE_SIZE as i32;

    if min_x < 0 {
        i -= 1;
    }
    if min_y < 0 {
        j -= 1;
    }

    (i, j)
}

#[derive(Debug)]
pub(crate) struct TileContourBuilder {
    tiles: HashSet<(i32, i32)>,
}

impl TileContourBuilder {
    pub fn new() -> Self {
        Self { tiles: HashSet::new() }
    }

    pub fn maxed() -> TileContour {
        TileContour::Maxed
    }

    pub fn enclose(&mut self, edge: &Edge<i32>) -> Option<(i32, i32)> {
        let (i, j) = edge_tile(edge);

        self.tiles.insert((j, i));
        Some((i, j))
    }

    fn enclose_tile(&mut self, tile: (i32, i32), mut translation: Point<i32>) {
        mem::swap(&mut translation.x, &mut translation.y);

        let translated =
            (tile.0 + translation.x / TILE_SIZE as i32, tile.1 + translation.y / TILE_SIZE as i32);
        self.tiles.insert(translated);

        if translation.x % TILE_SIZE as i32 != 0 {
            if translation.x > 0 {
                self.tiles.insert((translated.0 + 1, translated.1));
            } else {
                self.tiles.insert((translated.0 - 1, translated.1));
            }
        }
        if translation.y % TILE_SIZE as i32 != 0 {
            if translation.y > 0 {
                self.tiles.insert((translated.0, translated.1 + 1));
            } else {
                self.tiles.insert((translated.0, translated.1 - 1));
            }
        }
        if translation.x % TILE_SIZE as i32 != 0 && translation.y % TILE_SIZE as i32 != 0 {
            match (translation.x > 0, translation.y > 0) {
                (true, true) => self.tiles.insert((translated.0 + 1, translated.1 + 1)),
                (true, false) => self.tiles.insert((translated.0 + 1, translated.1 - 1)),
                (false, true) => self.tiles.insert((translated.0 - 1, translated.1 + 1)),
                (false, false) => self.tiles.insert((translated.0 - 1, translated.1 - 1)),
            };
        }
    }

    pub fn build(self) -> TileContour {
        #[cfg(feature = "tracing")]
        duration!("gfx", "TileContourBuilder::build");
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

#[derive(Clone, Debug)]
pub(crate) enum TileContour {
    Tiles(Vec<(i32, i32)>),
    Maxed,
}

impl TileContour {
    pub fn for_each_tile(&self, map: &mut Map, mut f: impl FnMut(&mut Tile)) {
        match self {
            Self::Tiles(tiles) => {
                for &(j, i) in tiles {
                    if 0 <= i
                        && 0 <= j
                        && (i as usize) < map.tile_width
                        && (j as usize) < map.tile_height
                    {
                        f(map.tile_mut(i as usize, j as usize));
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
                    let start = match tiles.binary_search(&(from_j as i32, from_i as i32)) {
                        Ok(i) | Err(i) => i,
                    };
                    let end = match tiles.binary_search(&(from_j as i32, map.tile_width as i32)) {
                        Ok(i) | Err(i) => i,
                    };

                    for &(j, i) in &tiles[start as usize..end as usize] {
                        f(map.tile_mut(i as usize, j as usize));
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

    pub fn translated(&self, translation: Point<i32>) -> Self {
        #[cfg(feature = "tracing")]
        duration!("gfx", "TileContour::translated");
        match self {
            Self::Tiles(tiles) => {
                let mut tile_contour_builder = TileContourBuilder::new();

                for tile in tiles {
                    tile_contour_builder.enclose_tile(*tile, translation);
                }

                tile_contour_builder.build()
            }
            Self::Maxed => self.clone(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Layer {
    raster: Raster,
    ops: Vec<TileOp>,
    new_edges: Cell<bool>,
}

impl Layer {
    pub fn new(raster: Raster, ops: Vec<TileOp>) -> Self {
        Self { raster, ops, new_edges: Cell::new(true) }
    }
}

#[derive(Debug, Default)]
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

        Self {
            tiles,
            layers: HashMap::new(),
            width,
            height,
            tile_width,
            tile_height,
        }
    }

    fn tile_mut(&mut self, i: usize, j: usize) -> &mut Tile {
        &mut self.tiles[i + j * self.tile_width]
    }

    pub fn global(&mut self, id: u32, ops: Vec<TileOp>) {
        self.print(id, Layer::new(Raster::maxed(), ops));
    }

    fn print_edge(
        &mut self,
        edge: &Edge<i32>,
        edge_range: Range<usize>,
        raster: &Raster,
        is_partial: bool,
    ) {
        let p0 = edge.p0;
        let edge = edge.translate(Point::new(
            raster.translation().x * PIXEL_WIDTH,
            raster.translation().y * PIXEL_WIDTH,
        ));
        let (i, j) = edge_tile(&edge);

        if 0 <= i && 0 <= j && (j as usize) < self.height {
            raster.tile_contour().for_each_tile_from(self, i as usize, j as usize, |tile| {
                if !is_partial || tile.needs_render {
                    tile.push_edge(p0, edge_range.clone());
                }
            });
        }
    }

    fn touch_tiles(&mut self, raster: Raster) {
        raster.tile_contour().for_each_tile(self, |tile| tile.needs_render = true);
    }

    pub fn print(&mut self, id: u32, layer: Layer) {
        if !layer.ops.is_empty() && self.layers.get(&id) != Some(&layer) {
            if let Some(old_layer) = self.layers.get(&id) {
                let raster = old_layer.raster.clone();
                self.touch_tiles(raster);
            }

            self.layers.insert(id, layer.clone());
            self.touch_tiles(layer.raster);
        }
    }

    pub fn remove(&mut self, id: u32) {
        if let Some(layer) = self.layers.get(&id) {
            let raster = layer.raster.clone();
            self.touch_tiles(raster);
            self.layers.remove(&id);
        }
    }

    fn specialized_print(&mut self, id: u32, is_partial: bool) {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Map::specialized_print");
        if let Some(layer) = self.layers.get(&id) {
            layer.new_edges.set(false);

            let raster = layer.raster.clone();
            raster.tile_contour().for_each_tile(self, |tile| {
                if !is_partial || tile.needs_render {
                    tile.new_layer(id, raster.translation());
                }
            });

            let mut edges = raster.edges().iter();
            let mut prev_index = edges.index();

            while let Some(edge) = edges.next() {
                self.print_edge(&edge, prev_index..edges.index(), &raster, is_partial);
                prev_index = edges.index();
            }
        }
    }

    fn reprint_all(&mut self) {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Map::reprint_all");
        let complete_reprints: HashSet<_> = self
            .layers
            .iter()
            .filter_map(|(id, layer)| if layer.new_edges.get() { Some(*id) } else { None })
            .collect();

        let mut partial_reprints = vec![];

        for id in &complete_reprints {
            let raster = self.layers.get(id).unwrap().raster.clone();
            raster.tile_contour().for_each_tile(self, |tile| tile.needs_render = true);
        }

        for tile in &mut self.tiles {
            if tile.needs_render {
                for node in &tile.layers {
                    if let LayerNode::Layer(id, _) = node {
                        if !complete_reprints.contains(id) {
                            partial_reprints.push(*id);
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
        reprints.dedup();

        for (id, is_partial) in reprints {
            self.specialized_print(id, is_partial);
        }
    }

    pub fn render<B: ColorBuffer>(&mut self, buffer: B) {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Map::render");
        self.reprint_all();

        let edges: HashMap<_, _> =
            self.layers.iter().map(|(&id, layer)| (id, layer.raster.edges())).collect();
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
                    let index = i + j * tile_width;
                    let tile = &tiles[index];

                    if tile.needs_render {
                        let context = Context {
                            tile,
                            index,
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

    fn all_tiles(
        tile_contour: &TileContour,
        map: &mut Map,
        from: Option<(usize, usize)>,
    ) -> Vec<(usize, usize)> {
        let mut tiles = vec![];

        match from {
            Some((from_i, from_j)) => {
                tile_contour.for_each_tile_from(map, from_i, from_j, |tile| {
                    tiles.push((tile.tile_i, tile.tile_j))
                })
            }
            None => tile_contour.for_each_tile(map, |tile| tiles.push((tile.tile_i, tile.tile_j))),
        }

        tiles
    }

    #[test]
    fn tile_contour_all_tiles() {
        let (mut map, tile_contour) = map_and_contour();

        assert_eq!(
            all_tiles(&tile_contour, &mut map, None),
            vec![(0, 0), (1, 0), (2, 0), (1, 1), (2, 1), (3, 1), (2, 2), (3, 2), (4, 2)]
        );
    }

    #[test]
    fn tile_contour_row_tiles() {
        let (mut map, tile_contour) = map_and_contour();

        assert_eq!(all_tiles(&tile_contour, &mut map, Some((2, 0))), vec![(2, 0)]);
        assert_eq!(all_tiles(&tile_contour, &mut map, Some((2, 1))), vec![(2, 1), (3, 1)]);
        assert_eq!(all_tiles(&tile_contour, &mut map, Some((2, 2))), vec![(2, 2), (3, 2), (4, 2)]);
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

    fn print(map: &mut Map, id: u32, raster: Raster) {
        map.print(id, Layer::new(raster, vec![TileOp::CoverWipNonZero]));
    }

    fn edges(raster: &Raster, range: Range<usize>) -> LayerNode {
        let mut edges = raster.edges().iter();
        let mut i = 0;

        let mut point = None;
        let mut start = None;
        let mut end = None;

        loop {
            let index = edges.index();

            if let Some(current_point) = edges.next().map(|edge| edge.p0) {
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
            end = Some(edges.index());
        }

        LayerNode::Edges(point.unwrap(), start.unwrap()..end.unwrap())
    }

    #[test]
    fn tile_edges_and_rasters() {
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
            vec![LayerNode::Layer(0, Point::new(0, 0)), edges(&raster, 0..HALF)],
        );
        assert_eq!(
            map.tiles[1].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                edges(&raster, 0..HALF),
                edges(&raster, HALF * 3..HALF * 4)
            ]
        );
        assert_eq!(map.tiles[2].layers, vec![]);
        assert_eq!(
            map.tiles[3].layers,
            vec![LayerNode::Layer(0, Point::new(0, 0)), edges(&raster, HALF..HALF * 2)]
        );
        assert_eq!(
            map.tiles[4].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                edges(&raster, HALF..HALF * 3),
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                edges(&translated, 0..HALF),
            ]
        );
        assert_eq!(
            map.tiles[5].layers,
            vec![
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                edges(&translated, 0..HALF),
                edges(&translated, HALF * 3..HALF * 4),
            ]
        );
        assert_eq!(map.tiles[6].layers, vec![]);
        assert_eq!(
            map.tiles[7].layers,
            vec![
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                edges(&translated, HALF..HALF * 2)
            ]
        );
        assert_eq!(
            map.tiles[8].layers,
            vec![
                LayerNode::Layer(1, Point::new(TILE_SIZE as i32, TILE_SIZE as i32)),
                edges(&translated, HALF..HALF * 3)
            ]
        );
    }

    #[test]
    fn reprint() {
        fn need_render(map: &Map) -> Vec<bool> {
            map.tiles.iter().map(|tile| tile.needs_render).collect()
        }

        let mut map = Map::new(TILE_SIZE * 4, TILE_SIZE * 4);

        let mut path = Path::new();
        polygon(&mut path, &[(0.5, 0.5), (0.5, 1.5), (1.5, 1.5), (1.5, 0.5)]);

        let mut raster = Raster::new(&path);

        map.global(0, vec![TileOp::ColorAccZero]);

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
                edges(&raster, 0..HALF),
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

        map.global(0, vec![TileOp::ColorAccZero]);
        map.print(1, Layer::new(raster.clone(), vec![TileOp::CoverWipNonZero]));

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

        map.global(0, vec![TileOp::ColorAccZero]);

        print(&mut map, 1, raster.clone());

        map.reprint_all();

        assert_eq!(
            map.tiles[0].layers,
            vec![
                LayerNode::Layer(0, Point::new(0, 0)),
                LayerNode::Layer(1, Point::new(0, 0)),
                edges(&raster, 0..TILE_SIZE),
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
                edges(&raster, 0..TILE_SIZE),
            ],
        );

        // Remove.
        map.remove(1);

        map.reprint_all();

        assert_eq!(map.tiles[0].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);

        assert_eq!(map.tiles[1].layers, vec![LayerNode::Layer(0, Point::new(0, 0))]);
    }

    #[test]
    fn tile_contour_translations() {
        let mut map = Map::new(TILE_SIZE * 6, TILE_SIZE * 6);

        let mut path = Path::new();
        polygon(&mut path, &[(2.5, 2.5), (2.5, 3.5), (3.5, 3.5), (3.5, 2.5)]);

        let mut raster = Raster::new(&path);

        assert_eq!(
            all_tiles(&*raster.tile_contour(), &mut map, None),
            vec![(2, 2), (3, 2), (2, 3), (3, 3)],
        );

        raster.set_translation(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));
        assert_eq!(
            all_tiles(&*raster.tile_contour(), &mut map, None),
            vec![(3, 3), (4, 3), (3, 4), (4, 4)],
        );

        raster.set_translation(Point::new(TILE_SIZE as i32 + 1, TILE_SIZE as i32 + 1));
        assert_eq!(
            all_tiles(&*raster.tile_contour(), &mut map, None),
            vec![(3, 3), (4, 3), (5, 3), (3, 4), (4, 4), (5, 4), (3, 5), (4, 5), (5, 5)],
        );

        raster.set_translation(Point::new(-(TILE_SIZE as i32), -(TILE_SIZE as i32)));
        assert_eq!(
            all_tiles(&*raster.tile_contour(), &mut map, None),
            vec![(1, 1), (2, 1), (1, 2), (2, 2)],
        );

        raster.set_translation(Point::new(-(TILE_SIZE as i32) - 1, -(TILE_SIZE as i32) - 1));
        assert_eq!(
            all_tiles(&*raster.tile_contour(), &mut map, None),
            vec![(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (2, 1), (0, 2), (1, 2), (2, 2)],
        );
    }

    #[test]
    fn tile_contour_translate_from_negative() {
        let mut map = Map::new(TILE_SIZE * 2, TILE_SIZE * 2);

        let mut path = Path::new();
        polygon(&mut path, &[(-1.5, -1.5), (-1.5, -0.5), (-0.5, -0.5), (-0.5, -1.5)]);

        let mut raster = Raster::new(&path);

        assert_eq!(all_tiles(&*raster.tile_contour(), &mut map, None), vec![]);

        raster.set_translation(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));
        assert_eq!(all_tiles(&*raster.tile_contour(), &mut map, None), vec![(0, 0)]);
    }
}
