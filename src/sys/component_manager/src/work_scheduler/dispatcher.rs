// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::{AbsoluteMoniker, Realm},
        work_scheduler::work_item::WorkItem,
    },
    fidl_fuchsia_sys2 as fsys,
    // fuchsia_vfs_pseudo_fs::directory,
    // futures::future::BoxFuture,
    // fuchsia_async as fasync,
    futures::future::BoxFuture,
};

/// A facade for `Realm` data needed by different parts of `work_scheduler`. This is abstracted into
/// a trait to facilitate unit testing when a `Realm` is not needed.
pub(super) trait Dispatcher: std::fmt::Debug + Send + Sync {
    /// Realms are identified by their `AbsoluteMoniker`.
    fn abs_moniker(&self) -> &AbsoluteMoniker;

    /// Initiate dispatch a series of `WorkItem`s in a new task.
    fn dispatch(&self, work_items: WorkItem) -> BoxFuture<Result<(), fsys::Error>>;
}

impl PartialEq for dyn Dispatcher {
    fn eq(&self, other: &Self) -> bool {
        self.abs_moniker() == other.abs_moniker()
    }
}
impl Eq for dyn Dispatcher {}

impl Dispatcher for Realm {
    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.abs_moniker
    }

    fn dispatch(&self, work_item: WorkItem) -> BoxFuture<Result<(), fsys::Error>> {
        Box::pin(async move { self.dispatch_async(work_item).await })
    }
}

impl Realm {
    async fn dispatch_async(&self, _work_item: WorkItem) -> Result<(), fsys::Error> {
        let execution_state = self.lock_execution().await;
        if let Some(_runtime) = &execution_state.runtime {
            // TODO(markdittmer): Use &runtime.exposed_dir.root_dir to connect to exposed
            // `Worker` interface and dispatch work.
        }

        Ok(())
    }
}
