// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{mem, ops::ControlFlow};

use crate::painter::{
    layer_workbench::{passes::PassesSharedState, Context, LayerWorkbenchState, TileWriteOp},
    LayerProps,
};

pub fn tile_unchanged_pass<'w, 'c, P: LayerProps>(
    workbench: &'w mut LayerWorkbenchState,
    state: &'w mut PassesSharedState,
    context: &'c Context<'_, P>,
) -> ControlFlow<TileWriteOp> {
    let clear_color_is_unchanged = context
        .previous_clear_color
        .map(|previous_clear_color| previous_clear_color == context.clear_color)
        .unwrap_or_default();

    let tile_paint = context.previous_layers.take().and_then(|previous_layers| {
        let layers = workbench.ids.len() as u32;

        let is_unchanged = if let Some(previous_layers) = previous_layers {
            let old_layers = mem::replace(previous_layers, layers);
            state.layers_were_removed = layers < old_layers;

            old_layers == layers && workbench.ids.iter().all(|&id| context.props.is_unchanged(id))
        } else {
            *previous_layers = Some(layers);
            false
        };

        (clear_color_is_unchanged && is_unchanged).then(|| TileWriteOp::None)
    });

    match tile_paint {
        Some(tile_paint) => ControlFlow::Break(tile_paint),
        None => ControlFlow::Continue(()),
    }
}
