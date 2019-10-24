// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt,
    iter::{self, FromIterator},
    ops::Range,
    rc::Rc,
};

#[cfg(feature = "tracing")]
use fuchsia_trace::duration;

use crate::{
    edge::Edge,
    path::Path,
    point::Point,
    tile::{TileContour, TileContourBuilder},
};

const COMPACT_DIFF_MASK: i32 = 0b111111;
const COMPACT_DIFF_DX_SHIFT: u16 = 6;
const COMPACT_DIFF_SHIFT_TO_I32: i32 = 32 - COMPACT_DIFF_DX_SHIFT as i32;

struct CompactDiff {
    value: u16,
}

impl CompactDiff {
    pub fn new(dx: i32, dy: i32) -> Self {
        let mut value = (RASTER_COMMAND_SEGMENT as u16) << 8;

        value |= ((dx & COMPACT_DIFF_MASK) as u16) << COMPACT_DIFF_DX_SHIFT;
        value |= (dy & COMPACT_DIFF_MASK) as u16;

        Self { value }
    }

    pub fn dx(&self) -> i32 {
        let shifted = self.value >> COMPACT_DIFF_DX_SHIFT;
        let dx = (shifted as i32 & COMPACT_DIFF_MASK) << COMPACT_DIFF_SHIFT_TO_I32;

        dx >> COMPACT_DIFF_SHIFT_TO_I32
    }

    pub fn dy(&self) -> i32 {
        let dy = (self.value as i32 & COMPACT_DIFF_MASK) << COMPACT_DIFF_SHIFT_TO_I32;

        dy >> COMPACT_DIFF_SHIFT_TO_I32
    }
}

const RASTER_COMMAND_MASK: u8 = 0b1000000;
const RASTER_COMMAND_MOVE: u8 = 0b0000000;
const RASTER_COMMAND_SEGMENT: u8 = 0b1000000;

pub struct RasterEdges {
    commands: Vec<u8>,
}

impl RasterEdges {
    pub fn new() -> Self {
        Self { commands: vec![] }
    }

    pub fn iter(&self) -> RasterEdgesIter {
        RasterEdgesIter { commands: &self.commands, index: 0, end_point: None }
    }

    pub fn from(&self, start_point: Point<i32>, range: Range<usize>) -> RasterEdgesIter {
        RasterEdgesIter { commands: &self.commands[range], index: 0, end_point: Some(start_point) }
    }
}

impl FromIterator<Edge<i32>> for RasterEdges {
    fn from_iter<T: IntoIterator<Item = Edge<i32>>>(iter: T) -> Self {
        let mut commands = vec![];
        let mut end_point = None;

        for edge in iter {
            if end_point != Some(edge.p0) {
                commands.push(RASTER_COMMAND_MOVE);

                commands.extend(&edge.p0.x.to_be_bytes());
                commands.extend(&edge.p0.y.to_be_bytes());
            }

            let diff = CompactDiff::new(edge.p1.x - edge.p0.x, edge.p1.y - edge.p0.y);
            commands.extend(&diff.value.to_be_bytes());

            end_point = Some(edge.p1);
        }

        Self { commands }
    }
}

impl fmt::Debug for RasterEdges {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

#[derive(Clone, Debug)]
pub struct RasterEdgesIter<'c> {
    commands: &'c [u8],
    index: usize,
    end_point: Option<Point<i32>>,
}

impl RasterEdgesIter<'_> {
    pub fn index(&self) -> usize {
        self.index
    }
}

impl Iterator for RasterEdgesIter<'_> {
    type Item = Edge<i32>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(command) = self.commands.get(self.index) {
            match command & RASTER_COMMAND_MASK {
                RASTER_COMMAND_MOVE => {
                    self.index += 1;

                    let mut bytes = [0u8; 4];

                    bytes.copy_from_slice(&self.commands[self.index..self.index + 4]);
                    let x = i32::from_be_bytes(bytes);
                    self.index += 4;

                    bytes.copy_from_slice(&self.commands[self.index..self.index + 4]);
                    let y = i32::from_be_bytes(bytes);
                    self.index += 4;

                    self.end_point = Some(Point::new(x, y));

                    self.next()
                }
                RASTER_COMMAND_SEGMENT => {
                    let mut bytes = [0u8; 2];

                    bytes.copy_from_slice(&self.commands[self.index..self.index + 2]);
                    let value = u16::from_be_bytes(bytes);
                    self.index += 2;

                    let diff = CompactDiff { value };

                    let start_point =
                        self.end_point.expect("RASTER_COMMAND_MOVE expected as first command");
                    self.end_point =
                        Some(Point::new(start_point.x + diff.dx(), start_point.y + diff.dy()));

                    Some(Edge::new(start_point, self.end_point.unwrap()))
                }
                _ => unreachable!(),
            }
        } else {
            None
        }
    }
}

#[doc(hidden)]
#[derive(Debug)]
pub struct RasterInner {
    edges: RasterEdges,
    tile_contour: TileContour,
}

impl RasterInner {
    #[doc(hidden)]
    pub fn translated(inner: &Rc<RasterInner>, translation: Point<i32>) -> Raster {
        Raster {
            inner: Rc::clone(inner),
            translation,
            translated_tile_contour: Some(inner.tile_contour.translated(translation)),
        }
    }
}

#[derive(Clone, Debug)]
pub struct Raster {
    #[doc(hidden)]
    pub inner: Rc<RasterInner>,
    translation: Point<i32>,
    translated_tile_contour: Option<TileContour>,
}

impl Raster {
    fn rasterize(edges: impl Iterator<Item = Edge<i32>>) -> RasterEdges {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Raster::rasterize");
        edges.collect()
    }

    fn build_contour(edges: &RasterEdges) -> TileContour {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Raster::tile_contour");
        let mut tile_contour_builder = TileContourBuilder::new();

        for edge in edges.iter() {
            tile_contour_builder.enclose(&edge);
        }

        tile_contour_builder.build()
    }

    fn from_edges(edges: impl Iterator<Item = Edge<i32>>) -> Self {
        let edges = Self::rasterize(edges);
        let tile_contour = Self::build_contour(&edges);

        Self {
            inner: Rc::new(RasterInner { edges, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub fn new(path: &Path) -> Self {
        Self::from_edges(path.edges().flat_map(|edge| edge.to_sp_edges()).flatten())
    }

    pub fn with_transform(path: &Path, transform: &[f32; 9]) -> Self {
        Self::from_edges(path.transformed(transform).flat_map(|edge| edge.to_sp_edges()).flatten())
    }

    pub fn empty() -> Self {
        Self::from_edges(iter::empty())
    }

    pub(crate) fn maxed() -> Self {
        let inner =
            RasterInner { edges: RasterEdges::new(), tile_contour: TileContourBuilder::maxed() };

        Self { inner: Rc::new(inner), translation: Point::new(0, 0), translated_tile_contour: None }
    }

    pub fn from_paths<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = &'a Path>,
    {
        Self::from_edges(
            paths
                .into_iter()
                .map(Path::edges)
                .flatten()
                .flat_map(|edge| edge.to_sp_edges())
                .flatten(),
        )
    }

    pub fn from_paths_and_transforms<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = (&'a Path, &'a [f32; 9])>,
    {
        Self::from_edges(
            paths
                .into_iter()
                .map(|(path, transform)| path.transformed(transform))
                .flatten()
                .flat_map(|edge| edge.to_sp_edges())
                .flatten(),
        )
    }

    pub fn translate(&mut self, translation: Point<i32>) {
        let translation =
            { Point::new(self.translation.x + translation.x, self.translation.y + translation.y) };
        self.set_translation(translation);
    }

    pub fn set_translation(&mut self, translation: Point<i32>) {
        let inner = &self.inner;

        if self.translation != translation {
            self.translation = translation;
            self.translated_tile_contour = Some(inner.tile_contour.translated(translation));
        }
    }

    pub fn union<'r>(rasters: impl Iterator<Item = &'r Self> + Clone) -> Self {
        let edges = rasters
            .clone()
            .map(|raster| raster.edges().iter().map(move |edge| edge.translate(raster.translation)))
            .flatten()
            .collect();
        let tile_contour = rasters.fold(TileContourBuilder::empty(), |tile_contour, raster| {
            tile_contour.union(raster.tile_contour())
        });

        Self {
            inner: Rc::new(RasterInner { edges, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub fn union_without_edges<'r>(rasters: impl Iterator<Item = &'r Self>) -> Self {
        let tile_contour = rasters.fold(TileContourBuilder::empty(), |tile_contour, raster| {
            tile_contour.union(raster.tile_contour())
        });

        Self {
            inner: Rc::new(RasterInner { edges: RasterEdges::new(), tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub(crate) fn edges(&self) -> &RasterEdges {
        &self.inner.edges
    }

    pub(crate) fn tile_contour(&self) -> &TileContour {
        self.translated_tile_contour.as_ref().unwrap_or(&self.inner.tile_contour)
    }

    pub(crate) fn translation(&self) -> Point<i32> {
        self.translation
    }
}

impl Eq for Raster {}

impl PartialEq for Raster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner) && self.translation == other.translation
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::PIXEL_WIDTH;

    fn to_and_fro(edges: &[Edge<i32>]) -> Vec<Edge<i32>> {
        edges.into_iter().cloned().collect::<RasterEdges>().iter().collect()
    }

    #[test]
    fn raster_edges_one_edge_all_end_point_combinations() {
        for x in -PIXEL_WIDTH..=PIXEL_WIDTH {
            for y in -PIXEL_WIDTH..=PIXEL_WIDTH {
                let edges = vec![Edge::new(Point::new(0, 0), Point::new(x, y))];

                assert_eq!(to_and_fro(&edges), edges);
            }
        }
    }

    #[test]
    fn raster_edges_one_edge_negative() {
        let edges = vec![Edge::new(Point::new(0, 0), Point::new(-PIXEL_WIDTH, -PIXEL_WIDTH))];

        assert_eq!(to_and_fro(&edges), edges);
    }

    #[test]
    fn raster_edges_two_edges_common() {
        let edges = vec![
            Edge::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
            Edge::new(
                Point::new(PIXEL_WIDTH, PIXEL_WIDTH),
                Point::new(PIXEL_WIDTH * 2, PIXEL_WIDTH * 2),
            ),
        ];

        assert_eq!(to_and_fro(&edges), edges);
    }

    #[test]
    fn raster_edges_two_edges_different() {
        let edges = vec![
            Edge::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
            Edge::new(
                Point::new(PIXEL_WIDTH * 5, PIXEL_WIDTH * 5),
                Point::new(PIXEL_WIDTH * 6, PIXEL_WIDTH * 6),
            ),
        ];

        assert_eq!(to_and_fro(&edges), edges);
    }

    #[test]
    fn raster_union() {
        let mut path1 = Path::new();
        path1.line(
            Point::new(0.0, 0.0),
            Point::new(1.0, 1.0),
        );

        let mut path2 = Path::new();
        path2.line(
            Point::new(1.0, 1.0),
            Point::new(2.0, 2.0),
        );

        let union = Raster::union([Raster::new(&path1), Raster::new(&path2)].into_iter());

        assert_eq!(union.inner.edges.iter().collect::<Vec<_>>(), vec![
            Edge::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
            Edge::new(
                Point::new(PIXEL_WIDTH, PIXEL_WIDTH),
                Point::new(PIXEL_WIDTH * 2, PIXEL_WIDTH * 2),
            ),
        ]);
        assert_eq!(union.inner.tile_contour.tiles(), vec![(0, 0)]);
    }

    #[test]
    fn raster_union_without_edges() {
        let mut path1 = Path::new();
        path1.line(
            Point::new(0.0, 0.0),
            Point::new(1.0, 1.0),
        );

        let mut path2 = Path::new();
        path2.line(
            Point::new(1.0, 1.0),
            Point::new(2.0, 2.0),
        );

        let union = Raster::union_without_edges([
            Raster::new(&path1),
            Raster::new(&path2),
        ].into_iter());

        assert_eq!(union.inner.edges.iter().collect::<Vec<_>>(), vec![]);
        assert_eq!(union.inner.tile_contour.tiles(), vec![(0, 0)]);
    }
}
