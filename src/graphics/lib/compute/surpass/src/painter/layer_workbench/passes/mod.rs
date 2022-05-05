// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rustc_hash::FxHashSet;

mod skip_fully_covered_layers;
mod skip_trivial_clips;
mod tile_unchanged;

pub use skip_fully_covered_layers::skip_fully_covered_layers_pass;
pub use skip_trivial_clips::skip_trivial_clips_pass;
pub use tile_unchanged::tile_unchanged_pass;

#[derive(Clone, Debug)]
pub struct PassesSharedState {
    pub skip_clipping: FxHashSet<u32>,
    pub layers_were_removed: bool,
}

impl Default for PassesSharedState {
    fn default() -> Self {
        Self { layers_were_removed: true, skip_clipping: FxHashSet::default() }
    }
}

impl PassesSharedState {
    pub fn reset(&mut self) {
        self.skip_clipping.clear();
        self.layers_were_removed = true;
    }
}
