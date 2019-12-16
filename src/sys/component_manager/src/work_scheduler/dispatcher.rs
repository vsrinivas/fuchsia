// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::{binding::Binder, error::ModelError, moniker::AbsoluteMoniker},
        work_scheduler::{work_item::WorkItem, work_scheduler::WORKER_CAPABILITY_PATH},
    },
    failure::Fail,
    fidl_fuchsia_io::{MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_async::Channel,
    fuchsia_zircon as zx,
    futures::future::BoxFuture,
    std::{
        hash::{Hash, Hasher},
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
    /// Realms are identified by their `AbsoluteMoniker`. Note that dispatchers with equal
    /// `absolute_moniker()` values must behave identically.
    fn abs_moniker(&self) -> &AbsoluteMoniker;

    /// Initiate dispatch a series of `WorkItem`s in a new task.
    fn dispatch(&self, work_items: Vec<WorkItem>) -> BoxFuture<Result<(), Error>>;
}

impl PartialEq for dyn Dispatcher {
    fn eq(&self, other: &Self) -> bool {
        self.abs_moniker() == other.abs_moniker()
    }
}
impl Eq for dyn Dispatcher {}

impl Hash for dyn Dispatcher {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.abs_moniker().hash(state);
    }
}

// TODO(markdittmer): `Realm` can be replaced by an `AbsoluteMoniker` when `Model` (and
// `Binder`) interface methods accept `AbsoluteMoniker` instead of `Realm`.
pub(super) struct RealDispatcher {
    /// `AbsoluteMoniker` of component instance receiving dispatch.
    target_moniker: AbsoluteMoniker,
    /// Implementation for binding to component and connecting to a service in its outgoing
    /// directory, in this case, the component instance's `fuchsia.sys2.Worker` server.
    binder: Weak<dyn Binder>,
}

impl RealDispatcher {
    pub(super) fn new(target_moniker: AbsoluteMoniker, binder: Weak<dyn Binder>) -> Arc<Self> {
        Arc::new(Self { target_moniker, binder })
    }
}

impl Dispatcher for RealDispatcher {
    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.target_moniker
    }

    fn dispatch(&self, work_items: Vec<WorkItem>) -> BoxFuture<Result<(), Error>> {
        Box::pin(async move { dispatch(&self.target_moniker, &self.binder, work_items).await })
    }
}

async fn dispatch(
    target_moniker: &AbsoluteMoniker,
    binder: &Weak<dyn Binder>,
    work_items: Vec<WorkItem>,
) -> Result<(), Error> {
    let (client_end, server_end) = zx::Channel::create().map_err(|err| Error::Internal(err))?;

    binder
        .upgrade()
        .ok_or_else(|| Error::ModelNotFound)?
        .bind(&target_moniker)
        .await
        .map_err(|err| Error::Model(err))?
        .open_outgoing(OPEN_RIGHT_READABLE, MODE_TYPE_SERVICE, &*WORKER_CAPABILITY_PATH, server_end)
        .await
        .map_err(|err| Error::Model(err))?;

    let client_end = Channel::from_channel(client_end).map_err(|err| Error::IO(err))?;
    let worker = fsys::WorkerProxy::new(client_end);

    for work_item in work_items.into_iter() {
        worker
            .do_work(&work_item.id)
            .await
            // TODO(fxb/42310): It may be advantageous to accumulate errors and continue iterating
            // over `work_items`.
            .map_err(|err| Error::FIDL(err))?
            .map_err(|err| Error::API(err))?;
    }

    Ok(())
}
