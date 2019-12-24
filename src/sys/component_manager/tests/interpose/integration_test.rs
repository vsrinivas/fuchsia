// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, async_trait::async_trait, breakpoint_system_client::*,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync, futures::channel::mpsc,
    futures::lock::Mutex, futures::SinkExt, futures::StreamExt, std::sync::Arc, test_utils::*,
};

/// Client <---> EchoInterposer <---> Echo service
/// The EchoInterposer copies all echo responses from the service
/// and sends them over an mpsc::Channel to the test.
struct EchoInterposer {
    tx: Mutex<mpsc::Sender<String>>,
}

impl EchoInterposer {
    fn new() -> (Arc<EchoInterposer>, mpsc::Receiver<String>) {
        let (tx, rx) = mpsc::channel(0);
        let tx = Mutex::new(tx);
        (Arc::new(EchoInterposer { tx }), rx)
    }
}

#[async_trait]
impl Interposer for EchoInterposer {
    type Marker = fecho::EchoMarker;

    async fn interpose(
        self: Arc<Self>,
        mut from_client: fecho::EchoRequestStream,
        to_service: fecho::EchoProxy,
    ) {
        // Start listening to requests from client
        while let Some(Ok(fecho::EchoRequest::EchoString { value: Some(input), responder })) =
            from_client.next().await
        {
            // Forward the request to the service and get a response
            let out = to_service
                .echo_string(Some(&input))
                .await
                .expect("echo_string failed")
                .expect("echo_string got empty result");

            // Copy the response from the service and send it to the test
            let mut tx = self.tx.lock().await;
            tx.send(out.clone()).await.expect("local tx/rx channel was closed");

            // Respond to the client with the response from the service
            responder.send(Some(out.as_str())).expect("failed to send echo response");
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn echo_test() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/interpose_integration_test#meta/echo_realm.cm",
    )
    .await?;

    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("breakpoint system is unavailable");
    let receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

    breakpoint_system.start_component_manager().await?;

    // Wait for echo_looper to attempt to connect to the Echo service
    let invocation = receiver
        .wait_until_component_capability("/echo_looper:0", "/svc/fidl.examples.routing.echo.Echo")
        .await?;

    // Setup the interposer
    let (interposer, mut rx) = EchoInterposer::new();
    invocation.interpose(interposer).await?;
    invocation.resume().await?;

    // Ensure that the string "Hippos rule!" is sent 10 times as a response
    // from server to client.
    for _ in 1..=10 {
        let echo_string = rx.next().await.expect("local tx/rx channel was closed");
        assert_eq!(echo_string, "Hippos rule!");
    }

    Ok(())
}
