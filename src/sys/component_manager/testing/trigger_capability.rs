// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::injectors::ProtocolInjector,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
};

#[must_use = "invoke resume() otherwise the client will be halted indefinitely!"]
pub struct Trigger {
    // This Sender is used to unblock the client that sent the trigger.
    responder: oneshot::Sender<()>,
}

impl Trigger {
    pub fn resume(self) {
        self.responder.send(()).unwrap()
    }
}

#[derive(Clone)]
pub struct TriggerSender {
    tx: Arc<Mutex<mpsc::Sender<Trigger>>>,
}

impl TriggerSender {
    fn new(tx: mpsc::Sender<Trigger>) -> Self {
        Self { tx: Arc::new(Mutex::new(tx)) }
    }

    /// Sends the event to a receiver. Returns a responder which can be blocked on.
    async fn send(&self) -> Result<oneshot::Receiver<()>, Error> {
        let (responder_tx, responder_rx) = oneshot::channel();
        {
            let mut tx = self.tx.lock().await;
            tx.send(Trigger { responder: responder_tx }).await?;
        }
        Ok(responder_rx)
    }
}

pub struct TriggerReceiver {
    rx: mpsc::Receiver<Trigger>,
}

impl TriggerReceiver {
    fn new(rx: mpsc::Receiver<Trigger>) -> Self {
        Self { rx }
    }

    /// Receives the next invocation from the sender.
    pub async fn next(&mut self) -> Option<Trigger> {
        self.rx.next().await
    }
}

/// Capability that serves the Trigger FIDL protocol in one tasks and allows
/// another task to wait on a trigger arriving via a TriggerReceiver.
#[derive(Clone)]
pub struct TriggerCapability {
    tx: TriggerSender,
}

impl TriggerCapability {
    pub fn new() -> (Arc<Self>, TriggerReceiver) {
        let (tx, rx) = mpsc::channel(0);
        let sender = TriggerSender::new(tx);
        let receiver = TriggerReceiver::new(rx);
        (Arc::new(Self { tx: sender }), receiver)
    }

    pub fn serve_async(self: Arc<Self>, request_stream: ftest::TriggerRequestStream) {
        fasync::Task::spawn(async move {
            self.serve(request_stream).await.expect("TriggerCapability injector failed");
        })
        .detach();
    }
}

#[async_trait]
impl ProtocolInjector for TriggerCapability {
    type Marker = ftest::TriggerMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: ftest::TriggerRequestStream,
    ) -> Result<(), Error> {
        while let Some(Ok(ftest::TriggerRequest::Run { responder })) = request_stream.next().await {
            let trigger = self.tx.send().await?;
            trigger.await?;
            responder.send("")?;
        }
        Ok(())
    }
}
