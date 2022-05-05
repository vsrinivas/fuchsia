// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::ControlFlow;

use crate::painter::{
    layer_workbench::{passes::PassesSharedState, Context, LayerWorkbenchState, TileWriteOp},
    BlendMode, Color, Fill, Func, LayerProps, Style,
};

pub fn skip_fully_covered_layers_pass<'w, 'c, P: LayerProps>(
    workbench: &'w mut LayerWorkbenchState,
    state: &'w mut PassesSharedState,
    context: &'c Context<'_, P>,
) -> ControlFlow<TileWriteOp> {
    #[derive(Debug)]
    enum InterestingCover {
        Opaque(Color),
        Incomplete,
    }

    let mut first_interesting_cover = None;
    // If layers were removed, we cannot assume anything because a visible layer
    // might have been removed since last frame.
    let mut visible_layers_are_unchanged = !state.layers_were_removed;
    for (i, &id) in workbench.ids.iter_masked().rev() {
        let props = context.props.get(id);

        if !context.props.is_unchanged(id) {
            visible_layers_are_unchanged = false;
        }

        let is_clipped = || {
            matches!(props.func, Func::Draw(Style { is_clipped: true, .. }))
                && !state.skip_clipping.contains(&id)
        };

        if is_clipped() || !workbench.layer_is_full(context, id, props.fill_rule) {
            if first_interesting_cover.is_none() {
                first_interesting_cover = Some(InterestingCover::Incomplete);
                // The loop does not break here in order to try to cull some layers that are
                // completely covered.
            }
        } else if let Func::Draw(Style {
            fill: Fill::Solid(color),
            blend_mode: BlendMode::Over,
            ..
        }) = props.func
        {
            if color.a == 1.0 {
                if first_interesting_cover.is_none() {
                    first_interesting_cover = Some(InterestingCover::Opaque(color));
                }

                workbench.ids.skip_until(i);

                break;
            }
        }
    }

    let (i, bottom_color) = match first_interesting_cover {
        // First opaque layer is skipped when blending.
        Some(InterestingCover::Opaque(color)) => {
            // All visible layers are unchanged so we can skip drawing altogether.
            if visible_layers_are_unchanged {
                return ControlFlow::Break(TileWriteOp::None);
            }

            (1, color)
        }
        // The clear color is used as a virtual first opqaue layer.
        None => (0, context.clear_color),
        // Visible incomplete cover makes full optimization impossible.
        Some(InterestingCover::Incomplete) => return ControlFlow::Continue(()),
    };

    let color = workbench.ids.iter_masked().skip(i).try_fold(bottom_color, |dst, (_, &id)| {
        match context.props.get(id).func {
            Func::Draw(Style { fill: Fill::Solid(color), blend_mode, .. }) => {
                Some(blend_mode.blend(dst, color))
            }
            // Fill is not solid.
            _ => None,
        }
    });

    match color {
        Some(color) => ControlFlow::Break(TileWriteOp::Solid(color)),
        None => ControlFlow::Continue(()),
    }
}
