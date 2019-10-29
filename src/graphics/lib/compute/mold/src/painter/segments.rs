// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    raster::{RasterSegments, RasterSegmentsIter},
    segment::Segment,
    tile::{LayerNode, Tile},
    Point,
};

#[derive(Clone, Debug)]
pub(crate) struct TileSegments<'t> {
    tile: &'t Tile,
    segments: &'t RasterSegments,
    index: usize,
    inner_segments: Option<RasterSegmentsIter<'t>>,
    pub translation: Point<i32>,
}

impl<'t> TileSegments<'t> {
    pub fn new(
        tile: &'t Tile,
        segments: &'t RasterSegments,
        index: usize,
        translation: Point<i32>,
    ) -> Self {
        Self { tile, segments, index, inner_segments: None, translation }
    }
}

impl<'t> Iterator for TileSegments<'t> {
    type Item = Segment<i32>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(segments) = self.inner_segments.as_mut() {
            if let Some(segment) = segments.next() {
                return Some(segment);
            }
        }

        if let Some(LayerNode::Segments(start_point, range)) = self.tile.layers.get(self.index) {
            self.index += 1;
            self.inner_segments = Some(self.segments.from(*start_point, range.clone()));

            return self.next();
        }

        None
    }
}
