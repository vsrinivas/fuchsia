// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    drawing::path_for_corner_knockouts,
    render::{BlendMode, Context as RenderContext, FillRule, Layer, Raster},
    Coord, Rect, Size,
};

pub use crate::scene::scene::SceneOrder;

/// Individual bits of UI
pub mod facets;
/// Grouping of facets
pub mod group;
/// Layout of facet groups
pub mod layout;
/// Rendering facets
pub mod scene;

struct Rendering {
    size: Size,
}

impl Rendering {
    fn new() -> Rendering {
        Rendering { size: Size::zero() }
    }
}

fn raster_for_corner_knockouts(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Raster {
    let path = path_for_corner_knockouts(bounds, corner_radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

/// Trait used by facets to mutate layers.
pub trait LayerGroup {
    /// Clears the group, removing all layers.
    fn clear(&mut self);

    /// Insert a order-layer pair into the group.
    fn insert(&mut self, order: SceneOrder, layer: Layer);

    /// Removes a layer from the group.
    fn remove(&mut self, order: SceneOrder);
}
