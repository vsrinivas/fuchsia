// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use surpass::{painter::Props, GeomId, GeomPresTransform, Order, Path};

use crate::small_bit_set::SmallBitSet;

use super::{interner::Interned, state::LayerSharedState};

#[derive(Debug)]
pub struct Layer {
    pub(crate) inner: surpass::Layer,
    pub(crate) shared_state: LayerSharedState,
    pub(crate) geom_id: GeomId,
    pub(crate) props: Interned<Props>,
    pub(crate) is_unchanged: SmallBitSet,
    pub(crate) lines_count: usize,
}

impl Layer {
    pub fn insert(&mut self, path: &Path) -> &mut Self {
        {
            let mut state = self.shared_state.inner();
            let builder = state.lines_builder.as_mut().expect("lines_builder should not be None");

            let old_len = builder.len();
            builder.push_path(self.geom_id, path);
            let len = builder.len() - old_len;

            state.geom_id_to_order.insert(self.geom_id, self.inner.order);

            self.lines_count += len;
        }

        self.is_unchanged.clear();
        self
    }

    pub fn clear(&mut self) -> &mut Self {
        {
            let mut state = self.shared_state.inner();

            state.geom_id_to_order.remove(&self.geom_id);

            self.geom_id = state.new_geom_id();
        }

        self.lines_count = 0;

        self.is_unchanged.clear();
        self
    }

    pub(crate) fn set_order(&mut self, order: Option<Order>) {
        if order.is_some() && self.inner.order != order {
            self.inner.order = order;
            self.is_unchanged.clear();
        }

        let geom_id = self.geom_id();
        self.shared_state.inner().geom_id_to_order.insert(geom_id, order);
    }

    pub fn geom_id(&self) -> GeomId {
        self.geom_id
    }

    pub(crate) fn is_unchanged(&self, cache_id: u8) -> bool {
        self.is_unchanged.contains(&cache_id)
    }

    pub(crate) fn set_is_unchanged(&mut self, cache_id: u8, is_unchanged: bool) -> bool {
        if is_unchanged {
            self.is_unchanged.insert(cache_id)
        } else {
            self.is_unchanged.remove(cache_id)
        }
    }

    #[inline]
    pub fn is_enabled(&self) -> bool {
        self.inner.is_enabled
    }

    #[inline]
    pub fn set_is_enabled(&mut self, is_enabled: bool) -> &mut Self {
        self.inner.is_enabled = is_enabled;
        self
    }

    #[inline]
    pub fn disable(&mut self) -> &mut Self {
        self.set_is_enabled(false)
    }

    #[inline]
    pub fn enable(&mut self) -> &mut Self {
        self.set_is_enabled(true)
    }

    #[inline]
    pub fn transform(&self) -> GeomPresTransform {
        self.inner.affine_transform.unwrap_or_default()
    }

    #[inline]
    pub fn set_transform(&mut self, transform: GeomPresTransform) -> &mut Self {
        // We want to perform a cheap check for the common case without hampering this function too
        // much.
        #[allow(clippy::float_cmp)]
        let affine_transform = if transform.is_identity() { None } else { Some(transform) };

        if self.inner.affine_transform != affine_transform {
            self.is_unchanged.clear();
            self.inner.affine_transform = affine_transform;
        }

        self
    }

    #[inline]
    pub fn props(&self) -> &Props {
        &self.props
    }

    #[inline]
    pub fn set_props(&mut self, props: Props) -> &mut Self {
        if *self.props != props {
            self.is_unchanged.clear();
            self.props = self.shared_state.inner().props_interner.get(props);
        }

        self
    }
}

impl Drop for Layer {
    fn drop(&mut self) {
        self.shared_state.inner().geom_id_to_order.remove(&self.geom_id);
    }
}
