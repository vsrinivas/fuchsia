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
    path::Path,
    point::Point,
    segment::Segment,
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

pub struct RasterSegment {
    commands: Vec<u8>,
}

impl RasterSegment {
    pub fn new() -> Self {
        Self { commands: vec![] }
    }

    pub fn iter(&self) -> RasterSegmentsIter {
        RasterSegmentsIter { commands: &self.commands, index: 0, end_point: None }
    }

    pub fn from(&self, start_point: Point<i32>, range: Range<usize>) -> RasterSegmentsIter {
        RasterSegmentsIter {
            commands: &self.commands[range],
            index: 0,
            end_point: Some(start_point),
        }
    }
}

impl FromIterator<Segment<i32>> for RasterSegment {
    fn from_iter<T: IntoIterator<Item = Segment<i32>>>(iter: T) -> Self {
        let mut commands = vec![];
        let mut end_point = None;

        for segment in iter {
            if end_point != Some(segment.p0) {
                commands.push(RASTER_COMMAND_MOVE);

                commands.extend(&segment.p0.x.to_be_bytes());
                commands.extend(&segment.p0.y.to_be_bytes());
            }

            let diff = CompactDiff::new(segment.p1.x - segment.p0.x, segment.p1.y - segment.p0.y);
            commands.extend(&diff.value.to_be_bytes());

            end_point = Some(segment.p1);
        }

        Self { commands }
    }
}

impl fmt::Debug for RasterSegment {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

#[derive(Clone, Debug)]
pub struct RasterSegmentsIter<'c> {
    commands: &'c [u8],
    index: usize,
    end_point: Option<Point<i32>>,
}

impl RasterSegmentsIter<'_> {
    pub fn index(&self) -> usize {
        self.index
    }
}

impl Iterator for RasterSegmentsIter<'_> {
    type Item = Segment<i32>;

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

                    Some(Segment::new(start_point, self.end_point.unwrap()))
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
    segments: RasterSegment,
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
    fn rasterize(segments: impl Iterator<Item = Segment<i32>>) -> RasterSegment {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Raster::rasterize");
        segments.collect()
    }

    fn build_contour(segments: &RasterSegment) -> TileContour {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Raster::tile_contour");
        let mut tile_contour_builder = TileContourBuilder::new();

        for segment in segments.iter() {
            tile_contour_builder.enclose(&segment);
        }

        tile_contour_builder.build()
    }

    fn from_segments(segments: impl Iterator<Item = Segment<i32>>) -> Self {
        let segments = Self::rasterize(segments);
        let tile_contour = Self::build_contour(&segments);

        Self {
            inner: Rc::new(RasterInner { segments, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub fn new(path: &Path) -> Self {
        Self::from_segments(path.segments().flat_map(|segment| segment.to_sp_segments()).flatten())
    }

    pub fn with_transform(path: &Path, transform: &[f32; 9]) -> Self {
        Self::from_segments(
            path.transformed(transform).flat_map(|segment| segment.to_sp_segments()).flatten(),
        )
    }

    pub fn empty() -> Self {
        Self::from_segments(iter::empty())
    }

    pub(crate) fn maxed() -> Self {
        let inner = RasterInner {
            segments: RasterSegment::new(),
            tile_contour: TileContourBuilder::maxed(),
        };

        Self { inner: Rc::new(inner), translation: Point::new(0, 0), translated_tile_contour: None }
    }

    pub fn from_paths<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = &'a Path>,
    {
        Self::from_segments(
            paths
                .into_iter()
                .map(Path::segments)
                .flatten()
                .flat_map(|segment| segment.to_sp_segments())
                .flatten(),
        )
    }

    pub fn from_paths_and_transforms<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = (&'a Path, &'a [f32; 9])>,
    {
        Self::from_segments(
            paths
                .into_iter()
                .map(|(path, transform)| path.transformed(transform))
                .flatten()
                .flat_map(|segment| segment.to_sp_segments())
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
        let segments = rasters
            .clone()
            .map(|raster| {
                raster.segments().iter().map(move |segment| segment.translate(raster.translation))
            })
            .flatten()
            .collect();
        let tile_contour = rasters.fold(TileContourBuilder::empty(), |tile_contour, raster| {
            tile_contour.union(raster.tile_contour())
        });

        Self {
            inner: Rc::new(RasterInner { segments, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub fn union_without_segments<'r>(rasters: impl Iterator<Item = &'r Self>) -> Self {
        let tile_contour = rasters.fold(TileContourBuilder::empty(), |tile_contour, raster| {
            tile_contour.union(raster.tile_contour())
        });

        Self {
            inner: Rc::new(RasterInner { segments: RasterSegment::new(), tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub(crate) fn segments(&self) -> &RasterSegment {
        &self.inner.segments
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

    fn to_and_fro(segments: &[Segment<i32>]) -> Vec<Segment<i32>> {
        segments.into_iter().cloned().collect::<RasterSegment>().iter().collect()
    }

    #[test]
    fn raster_segments_one_segment_all_end_point_combinations() {
        for x in -PIXEL_WIDTH..=PIXEL_WIDTH {
            for y in -PIXEL_WIDTH..=PIXEL_WIDTH {
                let segments = vec![Segment::new(Point::new(0, 0), Point::new(x, y))];

                assert_eq!(to_and_fro(&segments), segments);
            }
        }
    }

    #[test]
    fn raster_segments_one_segment_negative() {
        let segments = vec![Segment::new(Point::new(0, 0), Point::new(-PIXEL_WIDTH, -PIXEL_WIDTH))];

        assert_eq!(to_and_fro(&segments), segments);
    }

    #[test]
    fn raster_segments_two_segments_common() {
        let segments = vec![
            Segment::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
            Segment::new(
                Point::new(PIXEL_WIDTH, PIXEL_WIDTH),
                Point::new(PIXEL_WIDTH * 2, PIXEL_WIDTH * 2),
            ),
        ];

        assert_eq!(to_and_fro(&segments), segments);
    }

    #[test]
    fn raster_segments_two_segments_different() {
        let segments = vec![
            Segment::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
            Segment::new(
                Point::new(PIXEL_WIDTH * 5, PIXEL_WIDTH * 5),
                Point::new(PIXEL_WIDTH * 6, PIXEL_WIDTH * 6),
            ),
        ];

        assert_eq!(to_and_fro(&segments), segments);
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

        assert_eq!(union.inner.segments.iter().collect::<Vec<_>>(), vec![
            Segment::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
            Segment::new(
                Point::new(PIXEL_WIDTH, PIXEL_WIDTH),
                Point::new(PIXEL_WIDTH * 2, PIXEL_WIDTH * 2),
            ),
        ]);
        assert_eq!(union.inner.tile_contour.tiles(), vec![(0, 0)]);
    }

    #[test]
    fn raster_union_without_segments() {
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

        let union = Raster::union_without_segments([
            Raster::new(&path1),
            Raster::new(&path2),
        ].into_iter());

        assert_eq!(union.inner.segments.iter().collect::<Vec<_>>(), vec![]);
        assert_eq!(union.inner.tile_contour.tiles(), vec![(0, 0)]);
    }
}
