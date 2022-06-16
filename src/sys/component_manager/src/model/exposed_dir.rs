// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::WeakComponentInstance, dir_tree::DirTree, error::ModelError,
        routing_fns::route_expose_fn,
    },
    cm_rust::ComponentDecl,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    std::sync::Arc,
    vfs::{
        directory::{entry::DirectoryEntry, immutable as pfs},
        execution_scope::ExecutionScope,
        path::Path,
    },
};

type Directory = Arc<pfs::Simple>;

/// A pseudo-directory that proxies open requests to capabilities that have been exposed
/// by a component in its manifest.
pub struct ExposedDir {
    root_dir: Directory,
    execution_scope: ExecutionScope,
}

impl ExposedDir {
    /// Creates a new ExposedDir with an explicit execution scope.
    pub fn new(
        scope: ExecutionScope,
        component: WeakComponentInstance,
        decl: ComponentDecl,
    ) -> Result<Self, ModelError> {
        let mut dir = pfs::simple();
        let tree = DirTree::build_from_exposes(route_expose_fn, component.clone(), decl);
        tree.install(&component.abs_moniker, &mut dir)?;
        Ok(ExposedDir { root_dir: dir, execution_scope: scope })
    }

    /// Opens a new connection to this ExposedDir that is closed once this ExposedDir is dropped.
    pub fn open(
        &self,
        flags: fio::OpenFlags,
        mode: u32,
        path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        self.root_dir.clone().open(self.execution_scope.clone(), flags, mode, path, server_end);
    }
}

impl Drop for ExposedDir {
    fn drop(&mut self) {
        // Explicitly call shutdown to terminate all outstanding requests to
        // this pseudo-directory. ExecutionScope is ref-counted so we can't rely
        // on its drop impl to clean things up at this point.
        self.execution_scope.shutdown();
    }
}
