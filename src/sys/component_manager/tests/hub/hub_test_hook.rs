// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{capability::*, model::*},
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

// A HubTestHook is a framework capability routing hook that injects a
// HubTestCapabilityProvider every time a connection is requested to connect to the
// 'fuchsia.sys.HubReport' framework capability.
pub struct HubTestHook {
    observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,

    // Block on this receiver to know when the component has stopped.
    component_stop_rx: Mutex<mpsc::Receiver<()>>,

    // This sender is cloned to each HubTestCapability
    component_stop_tx: mpsc::Sender<()>,
}

impl HubTestHook {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::channel(0);
        HubTestHook {
            observers: Arc::new(Mutex::new(HashMap::new())),
            component_stop_rx: Mutex::new(rx),
            component_stop_tx: tx,
        }
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
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match (capability_provider, capability) {
            (None, ComponentManagerCapability::ServiceProtocol(source_path))
                if *source_path == *HUB_REPORT_SERVICE =>
            {
                return Ok(Some(Box::new(HubTestCapabilityProvider::new(
                    self.observers.clone(),
                    self.component_stop_tx.clone(),
                )) as Box<dyn ComponentManagerCapabilityProvider>))
            }
            (c, _) => return Ok(c),
        };
    }

    /// Wait for a component connected to the HubReport service to stop
    pub async fn wait_for_component_stop(&self) {
        let mut component_stop_rx = self.component_stop_rx.lock().await;
        component_stop_rx.next().await.expect("component stop channel has been closed");
    }
}

impl Hook for HubTestHook {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<'_, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::RouteFrameworkCapability { capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_framework_capability_async(
                            &capability,
                            capability_provider.take(),
                        )
                        .await?;
                }
                _ => {}
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
pub struct HubTestCapabilityProvider {
    // Path to directory listing.
    observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,

    // Sender used to notify when component has disconnected from underlying zx::Channel
    component_stop_tx: mpsc::Sender<()>,
}

impl HubTestCapabilityProvider {
    pub fn new(
        observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,
        component_stop_tx: mpsc::Sender<()>,
    ) -> Self {
        Self { observers, component_stop_tx }
    }

    pub async fn open_async(&self, server_end: zx::Channel) -> Result<(), ModelError> {
        let mut stream = ServerEnd::<fhub::HubReportMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        let observers = self.observers.clone();
        let mut component_stop_tx = self.component_stop_tx.clone();
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

            // Notify HubTestHook that the component has been stopped
            component_stop_tx.send(()).await.expect("component stop channel has been closed");
        });
        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for HubTestCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<'_, Result<(), ModelError>> {
        Box::pin(self.open_async(server_chan))
    }
}
