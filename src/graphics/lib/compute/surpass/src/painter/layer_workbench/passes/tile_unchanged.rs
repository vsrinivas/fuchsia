// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::ControlFlow;

use crate::painter::{
    layer_workbench::{
        passes::PassesSharedState, Context, LayerWorkbenchState, OptimizerTileWriteOp,
    },
    LayerProps,
};

pub fn tile_unchanged_pass<'w, 'c, P: LayerProps>(
    workbench: &'w mut LayerWorkbenchState,
    state: &'w mut PassesSharedState,
    context: &'c Context<'_, P>,
) -> ControlFlow<OptimizerTileWriteOp> {
    let clear_color_is_unchanged = context
        .cached_clear_color
        .map(|previous_clear_color| previous_clear_color == context.clear_color)
        .unwrap_or_default();

    let tile_paint = context.cached_tile.as_ref().and_then(|cached_tile| {
        let layers = workbench.ids.len() as u32;
        let previous_layers = cached_tile.update_layer_count(Some(layers));

        let is_unchanged = previous_layers
            .map(|previous_layers| {
                state.layers_were_removed = layers < previous_layers;

                previous_layers == layers
                    && workbench.ids.iter().all(|&id| context.props.is_unchanged(id))
            })
            .unwrap_or_default();

        (clear_color_is_unchanged && is_unchanged).then(|| OptimizerTileWriteOp::None)
    });

    match tile_paint {
        Some(tile_paint) => ControlFlow::Break(tile_paint),
        None => ControlFlow::Continue(()),
    }
}
