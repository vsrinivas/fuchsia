// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::directory,
    futures::future::{AbortHandle, Abortable},
};

/// Represents a component's directory of exposed capabilities.
pub struct ExposedDir {
    pub root_dir: directory::controlled::Controller<'static>,
    dir_abort_handles: Vec<AbortHandle>,
}

impl Drop for ExposedDir {
    fn drop(&mut self) {
        for abort_handle in &self.dir_abort_handles {
            abort_handle.abort();
        }
    }
}

impl ExposedDir {
    pub fn new(
        model: &Model,
        abs_moniker: &AbsoluteMoniker,
        realm_state: &RealmState,
    ) -> Result<Self, ModelError> {
        let mut dir_abort_handles = vec![];
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        dir_abort_handles.push(abort_handle);
        let (controller, mut controlled) =
            directory::controlled::controlled(directory::simple::empty());
        let route_fn = RoutingFacade::new(model.clone()).route_expose_fn_factory();
        let tree = DirTree::build_from_exposes(
            route_fn,
            abs_moniker,
            realm_state.decl.clone().expect("no decl"),
        );
        tree.install(abs_moniker, &mut controlled)?;
        let future = Abortable::new(controlled, abort_registration);
        fasync::spawn(async move {
            // Drop unused return value.
            let _ = future.await;
        });
        Ok(ExposedDir { root_dir: controller, dir_abort_handles })
    }
}
