// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::{AbsoluteMoniker, Binder, ModelError, Realm},
        work_scheduler::{work_item::WorkItem, work_scheduler::WORKER_CAPABILITY_PATH},
    },
    failure::Fail,
    fidl_fuchsia_io::{MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_async::Channel,
    fuchsia_zircon as zx,
    futures::future::BoxFuture,
    std::{
        io,
        sync::{Arc, Weak},
    },
};

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "fuchsia.sys2 fidl protocol error: {:?}", 0)]
    API(fsys::Error),
    /// Used in tests to indicate dispatchers that cannot connect to real component instances.
    #[cfg(test)]
    #[fail(display = "component is not running")]
    ComponentNotRunning,

    #[fail(display = "fidl error: {:?}", 0)]
    FIDL(fidl::Error),
    #[fail(display = "syscall failed with status {:?}", 0)]
    Internal(zx::Status),
    #[fail(display = "io errror: {:?}", 0)]
    IO(io::Error),
    #[fail(display = "component model error: {:?}", 0)]
    Model(ModelError),
    #[fail(display = "model has been dropped")]
    ModelNotFound,
}

/// A facade for dispatcher data needed by different parts of `WorkSchedulerDelegate`. This is
/// abstracted into a trait to facilitate unit testing when a real data is not needed.
pub(super) trait Dispatcher: Send + Sync {
    /// Realms are identified by their `AbsoluteMoniker`.
    fn abs_moniker(&self) -> &AbsoluteMoniker;

    /// Initiate dispatch a series of `WorkItem`s in a new task.
    fn dispatch(&self, work_items: WorkItem) -> BoxFuture<Result<(), Error>>;
}

impl PartialEq for dyn Dispatcher {
    fn eq(&self, other: &Self) -> bool {
        self.abs_moniker() == other.abs_moniker()
    }
}
impl Eq for dyn Dispatcher {}

// TODO(markdittmer): `Realm` can be replaced by an `AbsoluteMoniker` when `Model` (and
// `Binder`) interface methods accept `AbsoluteMoniker` instead of `Realm`.
pub(super) struct RealDispatcher {
    /// `Realm` where component instance receiving dispatch resides.
    realm: Arc<Realm>,
    /// Implementation for binding to component and connecting to a service in its outgoing
    /// directory, in this case, the component instance's `fuchsia.sys2.Worker` server.
    binder: Weak<dyn Binder>,
}

impl RealDispatcher {
    pub(super) fn new(realm: Arc<Realm>, binder: Weak<dyn Binder>) -> Arc<Self> {
        Arc::new(Self { realm, binder })
    }
}

impl Dispatcher for RealDispatcher {
    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.realm.abs_moniker
    }

    fn dispatch(&self, work_item: WorkItem) -> BoxFuture<Result<(), Error>> {
        Box::pin(async move { dispatch(&self.realm, &self.binder, work_item).await })
    }
}

async fn dispatch(
    realm: &Arc<Realm>,
    binder: &Weak<dyn Binder>,
    work_item: WorkItem,
) -> Result<(), Error> {
    let (client_end, server_end) = zx::Channel::create().map_err(|err| Error::Internal(err))?;

    binder
        .upgrade()
        .ok_or_else(|| Error::ModelNotFound)?
        .bind(&realm.abs_moniker)
        .await
        .map_err(|err| Error::Model(err))?
        .open_outgoing(OPEN_RIGHT_READABLE, MODE_TYPE_SERVICE, &*WORKER_CAPABILITY_PATH, server_end)
        .await
        .map_err(|err| Error::Model(err))?;

    let client_end = Channel::from_channel(client_end).map_err(|err| Error::IO(err))?;
    let worker = fsys::WorkerProxy::new(client_end);
    worker
        .do_work(&work_item.id)
        .await
        .map_err(|err| Error::FIDL(err))?
        .map_err(|err| Error::API(err))
}
