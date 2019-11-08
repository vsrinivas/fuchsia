// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::{AbsoluteMoniker, Realm},
        work_scheduler::{work_item::WorkItem, work_scheduler::WORKER_CAPABILITY_PATH},
    },
    failure::Fail,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_async::Channel,
    fuchsia_zircon as zx,
    futures::future::BoxFuture,
    std::io,
};

#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "syscall failed with status {:?}", 0)]
    Internal(zx::Status),
    #[fail(display = "component is not running")]
    ComponentNotRunning,
    #[fail(display = "fidl error: {:?}", 0)]
    FIDL(fidl::Error),
    #[fail(display = "io errror: {:?}", 0)]
    IO(io::Error),
    #[fail(display = "fuchsia.sys2 fidl protocol error: {:?}", 0)]
    API(fsys::Error),
}

/// A facade for `Realm` data needed by different parts of `work_scheduler`. This is abstracted into
/// a trait to facilitate unit testing when a `Realm` is not needed.
pub(super) trait Dispatcher: std::fmt::Debug + Send + Sync {
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

impl Dispatcher for Realm {
    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.abs_moniker
    }

    fn dispatch(&self, work_item: WorkItem) -> BoxFuture<Result<(), Error>> {
        Box::pin(async move { dispatch(self, work_item).await })
    }
}

async fn dispatch(realm: &Realm, work_item: WorkItem) -> Result<(), Error> {
    let (client_end, server_end) = zx::Channel::create().map_err(|err| Error::Internal(err))?;
    {
        let execution_state = realm.lock_execution().await;
        let runtime = execution_state.runtime.as_ref().ok_or(Error::ComponentNotRunning)?;
        let outgoing_dir = runtime.outgoing_dir.as_ref().ok_or(Error::ComponentNotRunning)?;

        outgoing_dir
            .open(
                OPEN_RIGHT_READABLE,
                MODE_TYPE_SERVICE,
                // DirectoryProxy.open() expects no leading "/":
                // "svc/[worker-service-name]", not "/svc/[worker-service-name]".
                &WORKER_CAPABILITY_PATH.to_string()[1..],
                ServerEnd::new(server_end),
            )
            .map_err(|err| Error::FIDL(err))?;
    }

    let client_end = Channel::from_channel(client_end).map_err(|err| Error::IO(err))?;
    let worker = fsys::WorkerProxy::new(client_end);
    worker
        .do_work(&work_item.id)
        .await
        .map_err(|err| Error::FIDL(err))?
        .map_err(|err| Error::API(err))
}
