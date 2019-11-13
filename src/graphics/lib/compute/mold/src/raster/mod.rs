// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{iter, rc::Rc};

#[cfg(feature = "tracing")]
use fuchsia_trace::duration;

use crate::{
    path::Path,
    point::Point,
    segment::Segment,
    tile::{TileContour, TileContourBuilder},
};

mod segments;

pub use segments::{RasterSegments, RasterSegmentsIter};

// Used in spinel-mold.
#[doc(hidden)]
#[derive(Debug)]
pub struct RasterInner {
    segments: RasterSegments,
    tile_contour: TileContour,
}

impl RasterInner {
    // Used in spinel-mold.
    #[doc(hidden)]
    pub fn translated(inner: &Rc<RasterInner>, translation: Point<i32>) -> Raster {
        Raster {
            inner: Rc::clone(inner),
            translation,
            translated_tile_contour: Some(inner.tile_contour.translated(translation)),
        }
    }
}

/// A rasterized, printable version of zero or more paths.
///
/// Rasters are pixel-grid-aware compact representations of vector content. They store information
/// only about the outlines that they define.
#[derive(Clone, Debug)]
pub struct Raster {
    // Used in spinel-mold.
    #[doc(hidden)]
    pub inner: Rc<RasterInner>,
    translation: Point<i32>,
    translated_tile_contour: Option<TileContour>,
}

impl Raster {
    fn rasterize(segments: impl Iterator<Item = Segment<i32>>) -> RasterSegments {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Raster::rasterize");
        segments.collect()
    }

    fn build_contour(segments: &RasterSegments) -> TileContour {
        #[cfg(feature = "tracing")]
        duration!("gfx", "Raster::tile_contour");
        let mut tile_contour_builder = TileContourBuilder::new();

        for segment in segments.iter() {
            tile_contour_builder.enclose(&segment);
        }

        tile_contour_builder.build()
    }

    fn from_segments(segments: impl Iterator<Item = Segment<f32>>) -> Self {
        let segments =
            Self::rasterize(segments.flat_map(|segment| segment.to_sp_segments()).flatten());
        let tile_contour = Self::build_contour(&segments);

        Self {
            inner: Rc::new(RasterInner { segments, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    /// Creates a new raster from a `path`.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Raster};
    /// let raster = Raster::new(&Path::new());
    /// ```
    pub fn new(path: &Path) -> Self {
        Self::from_segments(path.segments())
    }

    /// Creates a new raster from a `path` by applying `transform`.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Raster};
    /// let double = [2.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 1.0];
    /// let raster = Raster::with_transform(&Path::new(), &double);
    /// ```
    pub fn with_transform(path: &Path, transform: &[f32; 9]) -> Self {
        Self::from_segments(path.transformed(transform))
    }

    /// Creates an empty raster.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::Raster;
    /// let raster = Raster::empty();
    /// ```
    pub fn empty() -> Self {
        Self::from_segments(iter::empty())
    }

    pub(crate) fn maxed() -> Self {
        let inner = RasterInner {
            segments: RasterSegments::new(),
            tile_contour: TileContourBuilder::maxed(),
        };

        Self { inner: Rc::new(inner), translation: Point::new(0, 0), translated_tile_contour: None }
    }

    /// Creates a new raster from an `Iterator` of `paths`.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Raster};
    /// use std::iter;
    /// let raster = Raster::from_paths(iter::once(&Path::new()));
    /// ```
    pub fn from_paths<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = &'a Path>,
    {
        Self::from_segments(paths.into_iter().map(Path::segments).flatten())
    }

    /// Creates a new raster from an `Iterator` `paths` of `(path, transform)` tuples.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Raster};
    /// use std::iter;
    /// let double = [2.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 1.0];
    /// let raster = Raster::from_paths_and_transforms(iter::once((&Path::new(), &double)));
    /// ```
    pub fn from_paths_and_transforms<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = (&'a Path, &'a [f32; 9])>,
    {
        Self::from_segments(
            paths.into_iter().map(|(path, transform)| path.transformed(transform)).flatten(),
        )
    }

    /// Translates raster by `translation` pixels.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Point, Raster};
    /// # let mut raster = Raster::empty();
    /// raster.translate(Point::new(1, 0));
    /// raster.translate(Point::new(1, 0)); // Adds to first translation.
    /// // Total Ox translation: 2
    /// ```
    pub fn translate(&mut self, translation: Point<i32>) {
        let translation =
            { Point::new(self.translation.x + translation.x, self.translation.y + translation.y) };
        self.set_translation(translation);
    }

    /// Sets raster translation to `translation` pixels.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Point, Raster};
    /// # let mut raster = Raster::empty();
    /// raster.set_translation(Point::new(1, 0));
    /// raster.set_translation(Point::new(1, 0)); // Resets first translation.
    /// // Total Ox translation: 1
    /// ```
    pub fn set_translation(&mut self, translation: Point<i32>) {
        let inner = &self.inner;

        if self.translation != translation {
            self.translation = translation;
            self.translated_tile_contour = Some(inner.tile_contour.translated(translation));
        }
    }

    fn from_segments_and_contour<'r>(segments: RasterSegments, tile_contour: TileContour) -> Self {
        Self {
            inner: Rc::new(RasterInner { segments, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    /// Creates a uinion-raster from all `rasters`.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::Raster;
    /// use std::iter;
    /// let union = Raster::union(iter::repeat(&Raster::empty()).take(3));
    /// ```
    pub fn union<'r>(rasters: impl Iterator<Item = &'r Self>) -> Self {
        let mut tile_contour = TileContourBuilder::empty();
        let segments = rasters
            .map(|raster| {
                tile_contour = tile_contour.union(raster.tile_contour());
                raster.segments().iter().map(move |segment| segment.translate(raster.translation))
            })
            .flatten()
            .collect();

        Self::from_segments_and_contour(segments, tile_contour)
    }

    /// Creates a uinion-raster from all `rasters` but discards all segment data.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::Raster;
    /// use std::iter;
    /// let union = Raster::union_without_segments(iter::repeat(&Raster::empty()).take(3));
    /// ```
    pub fn union_without_segments<'r>(rasters: impl Iterator<Item = &'r Self>) -> Self {
        Self::from_segments_and_contour(
            RasterSegments::new(),
            rasters.fold(TileContourBuilder::empty(), |tile_contour, raster| {
                tile_contour.union(raster.tile_contour())
            }),
        )
    }

    pub(crate) fn segments(&self) -> &RasterSegments {
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
        segments.into_iter().cloned().collect::<RasterSegments>().iter().collect()
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
        path1.line(Point::new(0.0, 0.0), Point::new(1.0, 1.0));

        let mut path2 = Path::new();
        path2.line(Point::new(1.0, 1.0), Point::new(2.0, 2.0));

        let union = Raster::union([Raster::new(&path1), Raster::new(&path2)].into_iter());

        assert_eq!(
            union.inner.segments.iter().collect::<Vec<_>>(),
            vec![
                Segment::new(Point::new(0, 0), Point::new(PIXEL_WIDTH, PIXEL_WIDTH)),
                Segment::new(
                    Point::new(PIXEL_WIDTH, PIXEL_WIDTH),
                    Point::new(PIXEL_WIDTH * 2, PIXEL_WIDTH * 2),
                ),
            ]
        );
        assert_eq!(union.inner.tile_contour.tiles(), vec![(0, 0)]);
    }

    #[test]
    fn raster_union_without_segments() {
        let mut path1 = Path::new();
        path1.line(Point::new(0.0, 0.0), Point::new(1.0, 1.0));

        let mut path2 = Path::new();
        path2.line(Point::new(1.0, 1.0), Point::new(2.0, 2.0));

        let union =
            Raster::union_without_segments([Raster::new(&path1), Raster::new(&path2)].into_iter());

        assert_eq!(union.inner.segments.iter().collect::<Vec<_>>(), vec![]);
        assert_eq!(union.inner.tile_contour.tiles(), vec![(0, 0)]);
    }
}
