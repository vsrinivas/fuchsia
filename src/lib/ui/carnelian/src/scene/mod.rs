// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

use crate::{
    drawing::path_for_corner_knockouts,
    render::{BlendMode, Context as RenderContext, FillRule, Layer, Raster},
    Coord, Rect, Size,
};
use std::{
    collections::BTreeMap,
    sync::atomic::{AtomicUsize, Ordering},
};

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

#[derive(Default)]
pub(crate) struct IdGenerator {}

impl Iterator for IdGenerator {
    type Item = usize;

    fn next(&mut self) -> Option<usize> {
        static NEXT_ID: AtomicUsize = AtomicUsize::new(100);
        let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
        // fetch_add wraps on overflow, which we'll use as a signal
        // that this generator is out of ids.
        if id == 0 {
            None
        } else {
            Some(id)
        }
    }
}

/// Group of layers created and modified by facets.
pub struct LayerGroup(BTreeMap<u16, Layer>);

impl LayerGroup {
    /// Clears the group, removing all layers.
    pub fn clear(&mut self) {
        self.0.clear();
    }

    /// Insert a order-layer pair into the group.
    ///
    /// If the group did not have a layer at this location present, None is returned.
    ///
    /// If the group did have a layer present at this location, the layer is updated,
    /// and the old layer is returned.
    pub fn insert(&mut self, order: u16, layer: Layer) -> Option<Layer> {
        self.0.insert(order, layer)
    }

    /// Removes a layer from the group, returning the layer if present.
    pub fn remove(&mut self, order: u16) -> Option<Layer> {
        self.0.remove(&order)
    }
}
