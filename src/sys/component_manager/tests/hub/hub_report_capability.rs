// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl::Channel,
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::{collections::HashMap, sync::Arc},
};

pub static HUB_REPORT_SERVICE: &str = "/svc/fuchsia.test.hub.HubReport";

#[derive(Debug)]
pub enum HubReportEvent {
    DirectoryListing { listing: Vec<String>, responder: fhub::HubReportListDirectoryResponder },
    FileContent { content: String, responder: fhub::HubReportReportFileContentResponder },
}

/// A futures channel between the task where the integration test is running
/// and the task where the HubReport protocol is being serviced.
pub struct HubReportChannel {
    pub receiver: Option<mpsc::Receiver<HubReportEvent>>,
    pub sender: Option<mpsc::Sender<HubReportEvent>>,
}

fn get_or_insert_channel(
    map: &mut HashMap<String, HubReportChannel>,
    path: String,
) -> &mut HubReportChannel {
    map.entry(path.clone()).or_insert({
        let (sender, receiver) = mpsc::channel(0);
        HubReportChannel { receiver: Some(receiver), sender: Some(sender) }
    })
}

/// Framework capability that serves the HubReport FIDL protocol
pub struct HubReportCapability {
    // Path <-> Channel mapping
    observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,

    // Block on this receiver to know when the channel has been closed
    channel_close_rx: mpsc::Receiver<()>,

    // This sender is cloned and moved into to the server
    channel_close_tx: mpsc::Sender<()>,
}

impl HubReportCapability {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::channel(0);
        Self {
            observers: Arc::new(Mutex::new(HashMap::new())),
            channel_close_rx: rx,
            channel_close_tx: tx,
        }
    }

    /// Given a path, blocks until the component responds with a directory listing
    /// or file content.
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

    /// Serves the server end of the HubReport FIDL protocol asynchronously.
    pub fn serve_async(&self) -> Box<dyn Fn(Channel) + Send> {
        let observers = self.observers.clone();
        let channel_close_tx = self.channel_close_tx.clone();
        Box::new(move |channel| {
            let observers = observers.clone();
            let channel_close_tx = channel_close_tx.clone();
            fasync::spawn(async move {
                Self::serve(observers, channel_close_tx, channel).await;
            })
        })
    }

    /// Serves HubReport FIDL requests over the provided channel
    async fn serve(
        observers: Arc<Mutex<HashMap<String, HubReportChannel>>>,
        mut channel_close_tx: mpsc::Sender<()>,
        server_end: Channel,
    ) {
        let mut stream = ServerEnd::<fhub::HubReportMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
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

        // Notify HubTestHook that the channel has been closed
        channel_close_tx.send(()).await.expect("component stop channel has been closed");
    }

    pub async fn wait_for_channel_close(mut self) {
        self.channel_close_rx.next().await.expect("component stop channel has been closed");
    }
}
