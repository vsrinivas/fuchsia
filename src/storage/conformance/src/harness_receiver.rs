// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_io_test as io_test,
    futures::{channel::mpsc, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
    test_utils_lib::events::Injector,
};

/// Capability that serves the HarnessReceiver FIDL protocol in one task and allows
/// another task to wait on the received Io1TestHarness. This allows io conformance tests
/// to finish set up before notifying the receiver, sending the connection to the harness.
/// This is done to prevent race conditions when connecting to the harness on setup.
#[derive(Clone)]
pub struct HarnessReceiver {
    tx: Arc<Mutex<mpsc::Sender<fidl::endpoints::ClientEnd<io_test::Io1TestHarnessMarker>>>>,
}

impl HarnessReceiver {
    /// Returns a HarnessReceiver and a channel to listen for received Io1TestHarness connections
    /// sent to the receiver.
    pub fn new(
    ) -> (Arc<Self>, mpsc::Receiver<fidl::endpoints::ClientEnd<io_test::Io1TestHarnessMarker>>)
    {
        let (tx, rx) = mpsc::channel(0);
        let tx = Arc::new(Mutex::new(tx));
        (Arc::new(Self { tx }), rx)
    }
}

#[async_trait]
impl Injector for HarnessReceiver {
    type Marker = io_test::HarnessReceiverMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: io_test::HarnessReceiverRequestStream,
    ) -> Result<(), Error> {
        // Start listening to requests from the client.
        while let Some(Ok(io_test::HarnessReceiverRequest::SendIo1Harness {
            harness,
            control_handle: _,
        })) = request_stream.next().await
        {
            let mut tx = self.tx.lock().await;
            tx.send(harness).await.expect("Could not send the harness to the test.");
        }
        Ok(())
    }
}
