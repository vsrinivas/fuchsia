// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod paths;
pub mod renderer;
pub use crate::renderer::{
    cell_size_from_cell_height, get_scale_factor, renderable_layers, FontSet, LayerContent, Offset,
    RenderableLayer, Renderer,
};
