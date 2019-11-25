// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    point::Point,
    raster::{Raster, RasterSegments},
    tile::{
        contour::{Contour, ContourBuilder},
        Op,
    },
};

const EMPTY_SEGMENTS: &RasterSegments = &RasterSegments::new();
const MAXED_CONTOUR: &Contour = &ContourBuilder::maxed();

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum Content {
    Raster(Raster),
    Maxed,
}

impl Content {
    pub fn segments(&self) -> &RasterSegments {
        match self {
            Self::Raster(raster) => raster.segments(),
            Self::Maxed => EMPTY_SEGMENTS,
        }
    }

    pub fn contour(&self) -> &Contour {
        match self {
            Self::Raster(raster) => raster.contour(),
            Self::Maxed => MAXED_CONTOUR,
        }
    }

    pub fn translation(&self) -> Point<i32> {
        match self {
            Self::Raster(raster) => raster.translation(),
            Self::Maxed => Point::new(0, 0),
        }
    }
}

/// A layer containing a raster and [`Op`]s related to it.
#[derive(Clone, Debug)]
pub struct Layer {
    pub(crate) content: Content,
    pub(crate) ops: Vec<Op>,
    pub(crate) needs_render: Cell<bool>,
    pub(crate) is_partial: Cell<bool>,
}

impl Layer {
    /// Creates a new layer from a `raster` and `ops` that will be executed on the [tiles] where
    /// this raster is printed.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Layer, Raster};
    /// let layer = Layer::new(Raster::empty(), vec![]);
    /// ```
    ///
    /// [tiles]: crate::tile
    pub fn new(raster: Raster, ops: Vec<Op>) -> Self {
        Self {
            content: Content::Raster(raster),
            ops,
            needs_render: Cell::new(true),
            is_partial: Cell::new(false),
        }
    }

    pub(crate) fn maxed(ops: Vec<Op>) -> Self {
        Self {
            content: Content::Maxed,
            ops,
            needs_render: Cell::new(true),
            is_partial: Cell::new(false),
        }
    }
}

impl Eq for Layer {}

impl PartialEq for Layer {
    fn eq(&self, other: &Self) -> bool {
        self.content == other.content && self.ops == other.ops
    }
}