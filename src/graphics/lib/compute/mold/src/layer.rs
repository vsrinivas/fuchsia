// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{raster::Raster, tile::TileOp};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Layer {
    pub(crate) raster: Raster,
    pub(crate) ops: Vec<TileOp>,
    pub(crate) new_segments: Cell<bool>,
    pub(crate) is_partial: Cell<bool>,
}

impl Layer {
    pub fn new(raster: Raster, ops: Vec<TileOp>) -> Self {
        Self { raster, ops, new_segments: Cell::new(true), is_partial: Cell::new(false) }
    }
}
