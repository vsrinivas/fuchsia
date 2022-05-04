// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::{RefCell, RefMut},
    mem,
    rc::Rc,
};

use rustc_hash::FxHashMap;
use surpass::{painter::Props, GeomId, LinesBuilder, Order};

use super::interner::Interner;

#[derive(Debug)]
pub struct LayerSharedStateInner {
    pub lines_builder: Option<LinesBuilder>,
    pub geom_id_to_order: FxHashMap<GeomId, Option<Order>>,
    pub props_interner: Interner<Props>,
    geom_id_generator: GeomId,
}

impl Default for LayerSharedStateInner {
    fn default() -> Self {
        Self {
            lines_builder: Some(LinesBuilder::default()),
            geom_id_to_order: FxHashMap::default(),
            props_interner: Interner::default(),
            geom_id_generator: GeomId::default(),
        }
    }
}

impl LayerSharedStateInner {
    pub fn new_geom_id(&mut self) -> GeomId {
        let prev = self.geom_id_generator;
        mem::replace(&mut self.geom_id_generator, prev.next())
    }
}

#[derive(Debug, Default)]
pub struct LayerSharedState {
    inner: Rc<RefCell<LayerSharedStateInner>>,
}

impl LayerSharedState {
    pub fn new(inner: Rc<RefCell<LayerSharedStateInner>>) -> Self {
        Self { inner }
    }

    pub fn inner(&mut self) -> RefMut<'_, LayerSharedStateInner> {
        self.inner.borrow_mut()
    }
}

impl PartialEq<Rc<RefCell<LayerSharedStateInner>>> for LayerSharedState {
    fn eq(&self, other: &Rc<RefCell<LayerSharedStateInner>>) -> bool {
        Rc::ptr_eq(&self.inner, other)
    }
}

// Safe as long as `inner` can only be accessed by `&mut self`.
unsafe impl Sync for LayerSharedState {}
