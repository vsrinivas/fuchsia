// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(feature = "tracing")]
use fuchsia_trace::duration;
use rustc_hash::FxHashSet;

use crate::{
    point::Point,
    segment::Segment,
    tile::{map::Tiles, Tile, TILE_MASK, TILE_SHIFT},
};

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub(crate) struct ContourTile {
    j: i32, // Order is important for the Ord implementation.
    i: i32,
}

impl ContourTile {
    pub fn new(i: i32, j: i32) -> Self {
        Self { i, j }
    }

    pub fn shifted(self, di: i32, dj: i32) -> Self {
        Self { i: self.i + di, j: self.j + dj }
    }
}

impl From<(i32, i32)> for ContourTile {
    fn from(tile: (i32, i32)) -> Self {
        ContourTile { i: tile.0, j: tile.1 }
    }
}

impl Into<(i32, i32)> for ContourTile {
    fn into(self) -> (i32, i32) {
        (self.i, self.j)
    }
}

#[derive(Debug)]
pub(crate) struct ContourBuilder {
    // Using FxHashSet here instead of HashSet has a noticeable impact in performance, especially
    // since in the case of geometry-heavy scenes that create a lot of segments. (e.g. 25% less time
    // spent rasterization when rendering a detailed map of Paris)
    //
    // Tile indices are outside of user-control and, while the theoretical bounds can be quite
    // large, in practice the bounds will be very tight so the attack surface for a possible DoS
    // attack is not large enough. (in an 8K display resolution, the HashSet can have at most 2
    // objects in one bucket before it gets reallocated)
    tiles: FxHashSet<ContourTile>,
}

impl ContourBuilder {
    pub fn new() -> Self {
        Self { tiles: FxHashSet::default() }
    }

    pub const fn empty() -> Contour {
        Contour::Tiles(Vec::new())
    }

    pub const fn maxed() -> Contour {
        Contour::Maxed
    }

    pub fn enclose(&mut self, segment: &Segment<i32>) -> Option<(i32, i32)> {
        let tile = segment.tile();

        self.tiles.insert(tile.into());
        Some(tile)
    }

    fn enclose_tile(&mut self, tile: ContourTile, translation: Point<i32>) {
        let translated = tile.shifted(translation.x >> TILE_SHIFT, translation.y >> TILE_SHIFT);
        self.tiles.insert(translated);

        let sub_pixel_x_translation = translation.x & TILE_MASK != 0;
        let sub_pixel_y_translation = translation.y & TILE_MASK != 0;

        if sub_pixel_x_translation {
            self.tiles.insert(translated.shifted(1, 0));
        }
        if sub_pixel_y_translation {
            self.tiles.insert(translated.shifted(0, 1));
        }
        if sub_pixel_x_translation && sub_pixel_y_translation {
            self.tiles.insert(translated.shifted(1, 1));
        }
    }

    pub fn build(self) -> Contour {
        #[cfg(feature = "tracing")]
        duration!("gfx", "ContourBuilder::build");
        let mut new_tiles = vec![];
        let mut tiles: Vec<_> = self.tiles.into_iter().collect();
        tiles.sort();
        let mut tiles = tiles.into_iter().peekable();

        while let Some(tile) = tiles.next() {
            let mut next = None;
            loop {
                if let Some(peek) = tiles.peek() {
                    if tile.j == peek.j {
                        next = tiles.next();
                        continue;
                    }
                }

                break;
            }

            if let Some(next) = next {
                for i in tile.i..=next.i {
                    new_tiles.push(ContourTile::new(i, tile.j));
                }

                continue;
            }

            new_tiles.push(tile);
        }

        Contour::Tiles(new_tiles)
    }
}

/// The optimized contour of a raster at a TILE_SIZE-level of detail. A limitation of the approach
/// is that it doesn't account for horizontal concavities.
///
/// For example, the following X shape would have a contour like this that only accounts for
/// vertical concavities:
///
/// ```text
/// X___X         XXXXX
/// _X_X_         _XXX_
/// __X__   -->   __X__
/// _X_X_         _XXX_
/// X___X         XXXXX
/// ```
///
/// It does however work very well with thin lines:
///
/// ```text
/// X____         X____
/// _X___         _X___
/// __X__   -->   __X__
/// ___X_         ___X_
/// ____X         ____X
/// ```
#[derive(Clone, Debug)]
pub(crate) enum Contour {
    Tiles(Vec<ContourTile>),
    Maxed,
}

impl Contour {
    pub fn is_empty(&self) -> bool {
        match self {
            Self::Tiles(tiles) => tiles.is_empty(),
            Self::Maxed => true,
        }
    }

    pub fn for_each_tile(&self, tiles: &mut Tiles, mut f: impl FnMut(&mut Tile)) {
        match self {
            Self::Tiles(inner_tiles) => {
                for tile in inner_tiles {
                    if 0 <= tile.i
                        && 0 <= tile.j
                        && (tile.i as usize) < tiles.width()
                        && (tile.j as usize) < tiles.height()
                    {
                        f(tiles.get_mut(tile.i as usize, tile.j as usize));
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
                    let start = match inner_tiles
                        .binary_search(&ContourTile::new(from_i as i32, from_j as i32))
                    {
                        Ok(i) | Err(i) => i,
                    };
                    let end = match inner_tiles
                        .binary_search(&ContourTile::new(tiles.width() as i32, from_j as i32))
                    {
                        Ok(i) | Err(i) => i,
                    };

                    for tile in &inner_tiles[start as usize..end as usize] {
                        f(tiles.get_mut(tile.i as usize, tile.j as usize));
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
        duration!("gfx", "Contour::translated");
        match self {
            Self::Tiles(tiles) => {
                let mut contour_builder = ContourBuilder::new();

                for tile in tiles {
                    contour_builder.enclose_tile(*tile, translation);
                }

                contour_builder.build()
            }
            Self::Maxed => self.clone(),
        }
    }

    #[cfg(test)]
    pub(crate) fn tiles(&self) -> Vec<(i32, i32)> {
        match self {
            Self::Tiles(tiles) => tiles.iter().map(|tile| (*tile).into()).collect(),
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

    fn map_and_contour() -> (Map, Contour) {
        let map = Map::new(TILE_SIZE * 5, TILE_SIZE * 3);
        let mut contour_builder = ContourBuilder::new();

        let segment = Segment::new(
            Point::new(TILE_SIZE as f32 * 3.0, TILE_SIZE as f32 * 3.0),
            Point::new(0.0, 0.0),
        );
        for segment in segment.to_sp_segments().unwrap() {
            contour_builder.enclose(&segment);
        }

        let segment = Segment::new(
            Point::new(TILE_SIZE as f32 * 5.0, TILE_SIZE as f32 * 3.0),
            Point::new(TILE_SIZE as f32 * 2.0, 0.0),
        );
        for segment in segment.to_sp_segments().unwrap() {
            contour_builder.enclose(&segment);
        }

        (map, contour_builder.build())
    }

    fn all_tiles(
        contour: &Contour,
        map: &mut Map,
        from: Option<(usize, usize)>,
    ) -> Vec<(usize, usize)> {
        let mut tiles = vec![];

        match from {
            Some((from_i, from_j)) => {
                contour.for_each_tile_from(map.tiles_mut(), from_i, from_j, |tile| {
                    tiles.push((tile.i, tile.j))
                })
            }
            None => contour.for_each_tile(map.tiles_mut(), |tile| tiles.push((tile.i, tile.j))),
        }

        tiles
    }

    #[test]
    fn contour_all_tiles() {
        let (mut map, contour) = map_and_contour();

        assert_eq!(
            all_tiles(&contour, &mut map, None),
            vec![(0, 0), (1, 0), (2, 0), (1, 1), (2, 1), (3, 1), (2, 2), (3, 2), (4, 2)],
        );
    }

    #[test]
    fn contour_row_tiles() {
        let (mut map, contour) = map_and_contour();

        assert_eq!(all_tiles(&contour, &mut map, Some((2, 0))), vec![(2, 0)]);
        assert_eq!(all_tiles(&contour, &mut map, Some((2, 1))), vec![(2, 1), (3, 1)]);
        assert_eq!(all_tiles(&contour, &mut map, Some((2, 2))), vec![(2, 2), (3, 2), (4, 2)]);
    }

    #[test]
    fn contour_translations() {
        let mut map = Map::new(TILE_SIZE * 6, TILE_SIZE * 6);

        let mut path = Path::new();
        polygon(&mut path, &[(2.5, 2.5), (2.5, 3.5), (3.5, 3.5), (3.5, 2.5)]);

        let mut raster = Raster::new(&path);

        assert_eq!(
            all_tiles(&*raster.contour(), &mut map, None),
            vec![(2, 2), (3, 2), (2, 3), (3, 3)],
        );

        raster.set_translation(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));
        assert_eq!(
            all_tiles(&*raster.contour(), &mut map, None),
            vec![(3, 3), (4, 3), (3, 4), (4, 4)],
        );

        raster.set_translation(Point::new(TILE_SIZE as i32 + 1, TILE_SIZE as i32 + 1));
        assert_eq!(
            all_tiles(&*raster.contour(), &mut map, None),
            vec![(3, 3), (4, 3), (5, 3), (3, 4), (4, 4), (5, 4), (3, 5), (4, 5), (5, 5)],
        );

        raster.set_translation(Point::new(-(TILE_SIZE as i32), -(TILE_SIZE as i32)));
        assert_eq!(
            all_tiles(&*raster.contour(), &mut map, None),
            vec![(1, 1), (2, 1), (1, 2), (2, 2)],
        );

        raster.set_translation(Point::new(-(TILE_SIZE as i32) - 1, -(TILE_SIZE as i32) - 1));
        assert_eq!(
            all_tiles(&*raster.contour(), &mut map, None),
            vec![(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (2, 1), (0, 2), (1, 2), (2, 2)],
        );
    }

    #[test]
    fn contour_translate_from_negative() {
        let mut map = Map::new(TILE_SIZE * 2, TILE_SIZE * 2);

        let mut path = Path::new();
        polygon(&mut path, &[(-1.5, -1.5), (-1.5, -0.5), (-0.5, -0.5), (-0.5, -1.5)]);

        let mut raster = Raster::new(&path);

        assert_eq!(all_tiles(&*raster.contour(), &mut map, None), vec![]);

        raster.set_translation(Point::new(TILE_SIZE as i32, TILE_SIZE as i32));
        assert_eq!(all_tiles(&*raster.contour(), &mut map, None), vec![(0, 0)]);
    }
}
