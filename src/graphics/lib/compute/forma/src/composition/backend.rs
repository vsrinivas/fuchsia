// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use surpass::{rasterizer::Rasterizer, TILE_HEIGHT, TILE_WIDTH};

pub trait Backend: fmt::Debug + Default {}

#[derive(Debug, Default)]
pub struct CpuBackend {
    pub(crate) rasterizer: Rasterizer<TILE_WIDTH, TILE_HEIGHT>,
}

impl Backend for CpuBackend {}
