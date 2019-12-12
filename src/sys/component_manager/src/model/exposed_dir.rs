// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        dir_tree::DirTree, error::ModelError, model::Model, moniker::AbsoluteMoniker,
        routing_facade::RoutingFacade,
    },
    cm_rust::ComponentDecl,
    fuchsia_vfs_pseudo_fs_mt::directory::immutable as pfs,
    std::sync::Arc,
};

type Directory = Arc<pfs::Simple>;

/// Represents a component's directory of exposed capabilities.
pub struct ExposedDir {
    pub root_dir: Directory,
}

impl ExposedDir {
    pub fn new(
        model: &Model,
        abs_moniker: &AbsoluteMoniker,
        decl: ComponentDecl,
    ) -> Result<Self, ModelError> {
        let mut dir = pfs::simple();
        let route_fn = RoutingFacade::new(model.clone()).route_expose_fn_factory();
        let tree = DirTree::build_from_exposes(route_fn, abs_moniker, decl);
        tree.install(abs_moniker, &mut dir)?;
        Ok(ExposedDir { root_dir: dir })
    }
}
