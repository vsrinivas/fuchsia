// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl_fidl_examples_routing_echo as fecho,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
    test_utils_lib::interposers::ProtocolInterposer,
};

/// Client <---> EchoInterposer <---> Echo service
/// The EchoInterposer copies all echo responses from the service
/// and sends them over an mpsc::Channel to the test, in addition
/// to sending them back to the client.
pub struct EchoInterposer {
    tx: Mutex<mpsc::Sender<String>>,
}

impl EchoInterposer {
    pub fn new() -> (Arc<EchoInterposer>, mpsc::Receiver<String>) {
        let (tx, rx) = mpsc::channel(0);
        let tx = Mutex::new(tx);
        (Arc::new(EchoInterposer { tx }), rx)
    }
}

#[async_trait]
impl ProtocolInterposer for EchoInterposer {
    type Marker = fecho::EchoMarker;

    async fn serve(
        self: Arc<Self>,
        mut from_client: fecho::EchoRequestStream,
        to_server: fecho::EchoProxy,
    ) -> Result<(), Error> {
        // Start listening to requests from client
        while let Some(Ok(fecho::EchoRequest::EchoString { value: Some(input), responder })) =
            from_client.next().await
        {
            // Modify the input from the client.
            let modified_input = format!("Interposed: {}", input);

            // Forward the request to the service and get a response
            let out = to_server
                .echo_string(Some(&modified_input))
                .await?
                .expect("echo_string got empty result");

            // Copy the response from the service and send it to the test
            let mut tx = self.tx.lock().await;
            tx.send(out.clone()).await?;

            // Respond to the client with the response from the service
            responder.send(Some(out.as_str()))?;
        }

        Ok(())
    }
}
