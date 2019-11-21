// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

#[cfg(feature = "tracing")]
use fuchsia_trace::duration;
use rustc_hash::FxHashSet;

use crate::{
    point::Point,
    segment::Segment,
    tile::{map::Tiles, Tile, TILE_MASK, TILE_SHIFT},
};

#[derive(Debug)]
pub(crate) struct TileContourBuilder {
    // Using FxHashSet here instead of HashSet has a noticeable impact in performance, especially
    // since in the case of geometry-heavy scenes that create a lot of segments. (e.g. 25%
    // less time spent rasterization when rendering a detailed map of Paris)
    //
    // Tile indices are outside of user-control and, while the theoretical bounds can be quite
    // large, in practice the bounds will be very tight so the attack surface for a possible DoS
    // attack is not large enough. (in an 8K display resolution, the HashSet can have at most 2
    // objects in one bucket before it gets reallocated)
    tiles: FxHashSet<(i32, i32)>,
}

impl TileContourBuilder {
    pub fn new() -> Self {
        Self { tiles: FxHashSet::default() }
    }

    pub fn empty() -> TileContour {
        TileContour::Tiles(vec![])
    }

    pub fn maxed() -> TileContour {
        TileContour::Maxed
    }

    pub fn enclose(&mut self, segment: &Segment<i32>) -> Option<(i32, i32)> {
        let (i, j) = segment.tile();

        self.tiles.insert((j, i));
        Some((i, j))
    }

    fn enclose_tile(&mut self, tile: (i32, i32), mut translation: Point<i32>) {
        mem::swap(&mut translation.x, &mut translation.y);

        let translated =
            (tile.0 + (translation.x >> TILE_SHIFT), tile.1 + (translation.y >> TILE_SHIFT));
        self.tiles.insert(translated);

        let sub_pixel_x_translation = translation.x & TILE_MASK != 0;
        let sub_pixel_y_translation = translation.y & TILE_MASK != 0;

        if sub_pixel_x_translation {
            self.tiles.insert((translated.0 + 1, translated.1));
        }
        if sub_pixel_y_translation {
            self.tiles.insert((translated.0, translated.1 + 1));
        }
        if sub_pixel_x_translation && sub_pixel_y_translation {
            self.tiles.insert((translated.0 + 1, translated.1 + 1));
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
    pub fn for_each_tile(&self, tiles: &mut Tiles, mut f: impl FnMut(&mut Tile)) {
        match self {
            Self::Tiles(inner_tiles) => {
                for &(j, i) in inner_tiles {
                    if 0 <= i
                        && 0 <= j
                        && (i as usize) < tiles.width()
                        && (j as usize) < tiles.height()
                    {
                        f(tiles.get_mut(i as usize, j as usize));
                    }
                }
            }
            Self::Maxed => {
                for j in 0..tiles.height() {
                    for i in 0..tiles.width() {
                        f(tiles.get_mut(i, j));
                    }
                }
            }
        }
    }

    pub fn for_each_tile_from(
        &self,
        tiles: &mut Tiles,
        from_i: usize,
        from_j: usize,
        mut f: impl FnMut(&mut Tile),
    ) {
        match self {
            Self::Tiles(inner_tiles) => {
                if from_i < tiles.width() && from_j < tiles.height() {
                    let start = match inner_tiles.binary_search(&(from_j as i32, from_i as i32)) {
                        Ok(i) | Err(i) => i,
                    };
                    let end =
                        match inner_tiles.binary_search(&(from_j as i32, tiles.width() as i32)) {
                            Ok(i) | Err(i) => i,
                        };

                    for &(j, i) in &inner_tiles[start as usize..end as usize] {
                        f(tiles.get_mut(i as usize, j as usize));
                    }
                }
            }
            Self::Maxed => {
                for i in from_i..tiles.width() {
                    f(tiles.get_mut(i, from_j));
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
                tiles.dedup();

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

    #[cfg(test)]
    pub(crate) fn tiles(&self) -> Vec<(i32, i32)> {
        match self {
            Self::Tiles(tiles) => tiles.clone(),
            Self::Maxed => vec![],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::tile::map::tests::polygon;
    use crate::{
        path::Path,
        raster::Raster,
        tile::{Map, TILE_SIZE},
    };

    fn map_and_contour() -> (Map, TileContour) {
        let map = Map::new(TILE_SIZE * 5, TILE_SIZE * 3);
        let mut tile_contour_builder = TileContourBuilder::new();

        let segment = Segment::new(
            Point::new(TILE_SIZE as f32 * 3.0, TILE_SIZE as f32 * 3.0),
            Point::new(0.0, 0.0),
        );
        for segment in segment.to_sp_segments().unwrap() {
            tile_contour_builder.enclose(&segment);
        }

        let segment = Segment::new(
            Point::new(TILE_SIZE as f32 * 5.0, TILE_SIZE as f32 * 3.0),
            Point::new(TILE_SIZE as f32 * 2.0, 0.0),
        );
        for segment in segment.to_sp_segments().unwrap() {
            tile_contour_builder.enclose(&segment);
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
                tile_contour.for_each_tile_from(map.tiles_mut(), from_i, from_j, |tile| {
                    tiles.push((tile.tile_i, tile.tile_j))
                })
            }
            None => tile_contour
                .for_each_tile(map.tiles_mut(), |tile| tiles.push((tile.tile_i, tile.tile_j))),
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
