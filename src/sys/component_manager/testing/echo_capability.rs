// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::injectors::ProtocolInjector,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fidl_examples_routing_echo as fecho,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
};

#[must_use = "invoke resume() otherwise the client will be halted indefinitely!"]
pub struct Echo {
    pub message: String,
    // This Sender is used to unblock the client that sent the echo.
    responder: oneshot::Sender<()>,
}

impl Echo {
    pub fn resume(self) {
        self.responder.send(()).unwrap()
    }
}

#[derive(Clone)]
pub struct EchoSender {
    tx: Arc<Mutex<mpsc::Sender<Echo>>>,
}

impl EchoSender {
    fn new(tx: mpsc::Sender<Echo>) -> Self {
        Self { tx: Arc::new(Mutex::new(tx)) }
    }

    /// Sends the event to a receiver. Returns a responder which can be blocked on.
    async fn send(&self, message: String) -> Result<oneshot::Receiver<()>, Error> {
        let (responder_tx, responder_rx) = oneshot::channel();
        {
            let mut tx = self.tx.lock().await;
            tx.send(Echo { message, responder: responder_tx }).await?;
        }
        Ok(responder_rx)
    }
}

pub struct EchoReceiver {
    rx: mpsc::Receiver<Echo>,
}

impl EchoReceiver {
    fn new(rx: mpsc::Receiver<Echo>) -> Self {
        Self { rx }
    }

    /// Receives the next invocation from the sender.
    pub async fn next(&mut self) -> Option<Echo> {
        self.rx.next().await
    }
}

/// Capability that serves the Echo FIDL protocol in one task and allows
/// another task to wait on a echo arriving via a EchoReceiver.
#[derive(Clone)]
pub struct EchoCapability {
    tx: EchoSender,
}

impl EchoCapability {
    pub fn new() -> (Arc<Self>, EchoReceiver) {
        let (tx, rx) = mpsc::channel(0);
        let sender = EchoSender::new(tx);
        let receiver = EchoReceiver::new(rx);
        (Arc::new(Self { tx: sender }), receiver)
    }
}

#[async_trait]
impl ProtocolInjector for EchoCapability {
    type Marker = fecho::EchoMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: fecho::EchoRequestStream,
    ) -> Result<(), Error> {
        // Start listening to requests from the client.
        while let Some(Ok(fecho::EchoRequest::EchoString { value: Some(input), responder })) =
            request_stream.next().await
        {
            let echo = self.tx.send(input.clone()).await?;
            echo.await?;
            // Respond to the client with the echo string.
            responder.send(Some(&input))?;
        }
        Ok(())
    }
}
