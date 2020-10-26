// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use crate::{context::ContextInner, spinel_sys::*};

#[derive(Debug)]
pub(crate) struct RasterInner {
    context: Rc<RefCell<ContextInner>>,
    pub(crate) spn_raster: SpnRaster,
}

impl RasterInner {
    fn new(context: &Rc<RefCell<ContextInner>>, spn_raster: SpnRaster) -> Self {
        Self { context: Rc::clone(context), spn_raster }
    }
}

impl Drop for RasterInner {
    fn drop(&mut self) {
        self.context.borrow_mut().discard_raster(self.spn_raster);
    }
}

/// Spinel raster created by a `RasterBuilder`. [spn_raster_t]
///
/// [spn_raster_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#139
#[derive(Clone, Debug)]
pub struct Raster {
    pub(crate) inner: Rc<RasterInner>,
}

impl Raster {
    pub(crate) fn new(context: &Rc<RefCell<ContextInner>>, spn_raster: SpnRaster) -> Self {
        Self { inner: Rc::new(RasterInner::new(context, spn_raster)) }
    }
}

impl Eq for Raster {}

impl PartialEq for Raster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}
