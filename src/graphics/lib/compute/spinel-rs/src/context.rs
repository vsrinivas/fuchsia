// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use crate::spinel_sys::*;

#[derive(Debug)]
pub(crate) struct ContextInner {
    pub(crate) spn_context: SpnContext,
    pub(crate) spn_path_builder: SpnPathBuilder,
    pub(crate) spn_raster_builder: SpnRasterBuilder,
    paths_to_release: Vec<SpnPath>,
    rasters_to_release: Vec<SpnRaster>,
}

impl ContextInner {
    fn new_path_builder(spn_context: SpnContext) -> SpnPathBuilder {
        let mut spn_path_builder = SpnPathBuilder::default();
        unsafe {
            spn_path_builder_create(spn_context, &mut spn_path_builder as *mut _).success();
        }

        spn_path_builder
    }

    fn new_raster_builder(spn_context: SpnContext) -> SpnRasterBuilder {
        let mut spn_raster_builder = SpnRasterBuilder::default();
        unsafe {
            spn_raster_builder_create(spn_context, &mut spn_raster_builder as *mut _).success();
        }

        spn_raster_builder
    }

    fn new() -> Self {
        let spn_context = SpnContext::default();

        Self {
            spn_context,
            spn_path_builder: Self::new_path_builder(spn_context),
            spn_raster_builder: Self::new_raster_builder(spn_context),
            paths_to_release: vec![],
            rasters_to_release: vec![],
        }
    }

    pub(crate) fn reset_path_builder(&mut self) {
        self.spn_path_builder = Self::new_path_builder(self.spn_context);
    }

    pub(crate) fn reset_raster_builder(&mut self) {
        self.spn_raster_builder = Self::new_raster_builder(self.spn_context);
    }

    pub(crate) fn discard_path(&mut self, spn_path: SpnPath) {
        self.paths_to_release.push(spn_path);
    }

    pub(crate) fn discard_raster(&mut self, spn_raster: SpnRaster) {
        self.rasters_to_release.push(spn_raster);
    }

    fn release_paths(&mut self) {
        unsafe {
            spn_path_release(
                self.spn_context,
                self.paths_to_release.as_ptr(),
                self.paths_to_release.len() as u32,
            )
            .success();
        }
        self.paths_to_release.clear();
    }

    fn release_rasters(&mut self) {
        unsafe {
            spn_raster_release(
                self.spn_context,
                self.rasters_to_release.as_ptr(),
                self.rasters_to_release.len() as u32,
            )
            .success();
        }
        self.rasters_to_release.clear();
    }
}

impl Drop for ContextInner {
    fn drop(&mut self) {
        self.release_paths();
        self.release_rasters();
        unsafe {
            spn_path_builder_release(self.spn_path_builder).success();
            spn_raster_builder_release(self.spn_raster_builder).success();
            spn_context_release(self.spn_context).success();
        }
    }
}

/// Spinel context. [spn_context_t]
///
/// Also creates and stores a [spn_path_builder_t] and a [spn_raster_builder_t].
/// [spn_path_builder_create] [spn_raster_builder_create]
///
/// [spn_context_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#15
/// [spn_path_builder_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#34
/// [spn_path_builder_create]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#39
/// [spn_raster_builder_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#123
/// [spn_raster_builder_create]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#128
#[derive(Clone, Debug)]
pub struct Context {
    pub(crate) inner: Rc<RefCell<ContextInner>>,
}

impl Context {
    // TODO: The current version of this constructor is only used in testing and will be replaced
    //       once Spinel's context initialization is added to spinel.h.
    #[allow(dead_code)]
    pub(crate) fn new() -> Self {
        Self { inner: Rc::new(RefCell::new(ContextInner::new())) }
    }
}

impl Eq for Context {}

impl PartialEq for Context {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}
