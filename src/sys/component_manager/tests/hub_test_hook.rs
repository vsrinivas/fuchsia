// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    cm_rust::UseDecl,
    component_manager_lib::{framework::FrameworkCapability, model::*},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{channel::*, future::BoxFuture, lock::Mutex, sink::SinkExt, StreamExt},
    lazy_static::lazy_static,
    std::{collections::HashMap, convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref HUB_REPORT_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.hub.HubReport".try_into().unwrap();
}

fn get_or_insert_channel<'a>(
    map: &'a mut HashMap<String, DirectoryListingChannel>,
    path: String,
) -> &'a mut DirectoryListingChannel {
    map.entry(path).or_insert({
        let (sender, receiver) = mpsc::channel(0);
        DirectoryListingChannel { receiver, sender }
    })
}

// A HubTestHook is a framework capability routing hook that injects a
// HubTestCapability every time a connection is requested to connect to the
// 'fuchsia.sys.HubReport' framework capability.
pub struct HubTestHook {
    observers: Arc<Mutex<HashMap<String, DirectoryListingChannel>>>,
}

impl HubTestHook {
    pub fn new() -> Self {
        HubTestHook { observers: Arc::new(Mutex::new(HashMap::new())) }
    }

    // Given a directory path, blocks the current task until a component
    // connects to the HubReport framework service and responds with a listing
    // of entries within that directory.
    pub async fn observe(&self, path: String) -> Vec<String> {
        let mut observers = await!(self.observers.lock());
        let channel = get_or_insert_channel(&mut observers, path);
        if let Some(listing) = await!(channel.receiver.next()) {
            return listing.listing;
        }
        return vec![];
    }

    pub async fn on_route_framework_capability_async<'a>(
        &'a self,
        use_decl: &'a UseDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        match (capability, use_decl) {
            (None, UseDecl::Service(s)) if s.source_path == *HUB_REPORT_SERVICE => {
                return Ok(Some(Box::new(HubTestCapability::new(self.observers.clone()))
                    as Box<dyn FrameworkCapability>))
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for HubTestHook {
    fn on_bind_instance<'a>(
        &'a self,
        _realm: Arc<Realm>,
        _realm_state: &'a RealmState,
        _routing_facade: RoutingFacade,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async { Ok(()) })
    }

    fn on_add_dynamic_child(&self, _realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async { Ok(()) })
    }

    fn on_remove_dynamic_child(&self, _realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async { Ok(()) })
    }

    fn on_route_framework_capability<'a>(
        &'a self,
        _realm: Arc<Realm>,
        use_decl: &'a UseDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> BoxFuture<Result<Option<Box<dyn FrameworkCapability>>, ModelError>> {
        Box::pin(self.on_route_framework_capability_async(use_decl, capability))
    }
}

pub struct DirectoryListing {
    pub path: String,
    pub listing: Vec<String>,
}

// A futures channel between the task where the integration test is running
// and the task where the HubReport protocol is being serviced.
pub struct DirectoryListingChannel {
    pub receiver: mpsc::Receiver<DirectoryListing>,
    pub sender: mpsc::Sender<DirectoryListing>,
}

// Corresponds to a connection to the framework service: HubReport.
pub struct HubTestCapability {
    // Path to directory listing.
    observers: Arc<Mutex<HashMap<String, DirectoryListingChannel>>>,
}

impl HubTestCapability {
    pub fn new(observers: Arc<Mutex<HashMap<String, DirectoryListingChannel>>>) -> Self {
        HubTestCapability { observers }
    }

    pub async fn open_async(&self, server_end: zx::Channel) -> Result<(), ModelError> {
        let mut stream = ServerEnd::<fhub::HubReportMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        let observers = self.observers.clone();
        fasync::spawn(async move {
            while let Some(Ok(request)) = await!(stream.next()) {
                let fhub::HubReportRequest::ListDirectory { path, entries, control_handle: _ } =
                    request;
                let mut observers = await!(observers.lock());
                let channel = get_or_insert_channel(&mut observers, path.clone());
                await!(channel
                    .sender
                    .send(DirectoryListing { path: path.clone(), listing: entries.clone() }))
                .expect("Unable to send");
            }
        });
        Ok(())
    }
}

impl FrameworkCapability for HubTestCapability {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(server_chan))
    }
}
