// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::FrameworkCapabilityDecl,
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
    map: &'a mut HashMap<String, HubReportChannel>,
    path: String,
) -> &'a mut HubReportChannel {
    map.entry(path.clone()).or_insert({
        let (sender, receiver) = mpsc::channel(0);
        HubReportChannel { receiver: Some(receiver), sender: Some(sender) }
    })
}

// A Breakpoint is an object created and sent to the integration test when a
// BreakpointEvent is triggered in ComponentManager. The object contains information
// about the Breakpoint (what event occurred and what realm it applies to) along with
// a means to resume/unblock the ComponentManager.
pub struct Breakpoint {
    pub event: EventType,
    pub moniker: AbsoluteMoniker,

    // This Sender is used by the integration test
    // to unblock the ComponentManager.
    responder: oneshot::Sender<()>,
}

impl Breakpoint {
    pub fn resume(self) {
        self.responder.send(()).unwrap()
    }
}

// Breakpoints is used to wait for a Breakpoint object or to send a Breakpoint object when
// a BreakpointEvent occurs.
pub struct Breakpoints {
    tx: Arc<Mutex<mpsc::Sender<Breakpoint>>>,
    rx: Arc<Mutex<mpsc::Receiver<Breakpoint>>>,
}

impl Breakpoints {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::channel(0);
        Self { tx: Arc::new(Mutex::new(tx)), rx: Arc::new(Mutex::new(rx)) }
    }

    pub async fn send(&self, event: EventType, moniker: AbsoluteMoniker) -> Result<(), ModelError> {
        let (responder_tx, responder_rx) = oneshot::channel();
        {
            let mut tx = self.tx.lock().await;
            tx.send(Breakpoint { event, moniker, responder: responder_tx }).await.unwrap();
        }
        responder_rx.await.unwrap();
        Ok(())
    }

    pub async fn receive(&self) -> Breakpoint {
        let mut rx = self.rx.lock().await;
        rx.next().await.expect("Breakpoint did not occur")
    }
}

// A HubTestHook is a framework capability routing hook that injects a
// HubTestCapability every time a connection is requested to connect to the
// 'fuchsia.sys.HubReport' framework capability.
pub struct HubTestHook {
    observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,
    breakpoints: Breakpoints,
}

impl HubTestHook {
    pub fn new() -> Self {
        HubTestHook {
            observers: Arc::new(Mutex::new(HashMap::new())),
            breakpoints: Breakpoints::new(),
        }
    }

    pub async fn wait_for_breakpoint(&self) -> Breakpoint {
        self.breakpoints.receive().await
    }

    // Given a directory path, blocks the current task until a component
    // connects to the HubReport framework service and responds with a listing
    // of entries within that directory.
    pub async fn observe(&self, path: &str) -> HubReportEvent {
        let mut receiver = {
            let mut observers = self.observers.lock().await;
            // Avoid holding onto to the observers lock while waiting for a
            // message to avoid deadlock.
            let channel = get_or_insert_channel(&mut observers, path.to_string());
            channel.receiver.take().unwrap()
        };
        let event = receiver.next().await.expect("Missing HubReportEvent");
        // Transfer ownership back to the observers HashMap after the listing has
        // been received.
        let mut observers = self.observers.lock().await;
        let channel = get_or_insert_channel(&mut observers, path.to_string());
        channel.receiver = Some(receiver);

        return event;
    }

    pub async fn on_route_framework_capability_async<'a>(
        &'a self,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapabilityDecl::LegacyService(source_path))
                if *source_path == *HUB_REPORT_SERVICE =>
            {
                return Ok(Some(Box::new(HubTestCapability::new(self.observers.clone()))
                    as Box<dyn FrameworkCapability>))
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for HubTestHook {
    fn on<'a>(self: Arc<Self>, event: &'a Event<'_>) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match event {
                Event::RouteFrameworkCapability { realm: _, capability_decl, capability } => {
                    let mut capability = capability.lock().await;
                    *capability = self
                        .on_route_framework_capability_async(capability_decl, capability.take())
                        .await?;
                }
                Event::PreDestroyInstance { parent_realm, child_moniker } => {
                    self.breakpoints.send(event.type_(), parent_realm.abs_moniker.child(child_moniker.clone())).await?;
                }
                Event::StopInstance { realm } => {
                    self.breakpoints.send(event.type_(), realm.abs_moniker.clone()).await?;
                }
                Event::DestroyInstance { realm } => {
                    self.breakpoints.send(event.type_(), realm.abs_moniker.clone()).await?;
                }
                _ => (),
            };
            Ok(())
        })
    }
}

#[derive(Debug)]
pub enum HubReportEvent {
    DirectoryListing { listing: Vec<String>, responder: fhub::HubReportListDirectoryResponder },
    FileContent { content: String, responder: fhub::HubReportReportFileContentResponder },
}

// A futures channel between the task where the integration test is running
// and the task where the HubReport protocol is being serviced.
pub struct HubReportChannel {
    pub receiver: Option<mpsc::Receiver<HubReportEvent>>,
    pub sender: Option<mpsc::Sender<HubReportEvent>>,
}

// Corresponds to a connection to the framework service: HubReport.
pub struct HubTestCapability {
    // Path to directory listing.
    observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,
}

impl HubTestCapability {
    pub fn new(observers: Arc<Mutex<HashMap<String, HubReportChannel>>>) -> Self {
        HubTestCapability { observers }
    }

    pub async fn open_async(&self, server_end: zx::Channel) -> Result<(), ModelError> {
        let mut stream = ServerEnd::<fhub::HubReportMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        let observers = self.observers.clone();
        fasync::spawn(async move {
            while let Some(Ok(request)) = stream.next().await {
                let (path, event) = match request {
                    fhub::HubReportRequest::ListDirectory { path, entries, responder } => {
                        (path, HubReportEvent::DirectoryListing { listing: entries, responder })
                    }
                    fhub::HubReportRequest::ReportFileContent { path, content, responder } => {
                        (path, HubReportEvent::FileContent { content, responder })
                    }
                };
                let mut sender = {
                    // Avoid holding onto to the observers lock while sending a
                    // message to avoid deadlock.
                    let mut observers = observers.lock().await;
                    let channel = get_or_insert_channel(&mut observers, path.clone());
                    channel.sender.take().unwrap()
                };
                sender.send(event).await.expect("Unable to send HubEvent.");
                // Transfer ownership back to the observers HashMap after the listing has
                // been sent.
                let mut observers = observers.lock().await;
                let channel = get_or_insert_channel(&mut observers, path.clone());
                channel.sender = Some(sender);
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
