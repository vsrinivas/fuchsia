// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    breakpoint_system_client::Interposer,
    echo_interposer::EchoInterposer,
    fidl_fuchsia_test_echofactory as fechofactory, fuchsia_async as fasync,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
};

/// Client <---> EchoFactoryInterposer <---> EchoFactory service
/// The EchoFactoryInterposer installs EchoInterposers on all echo
/// protocols and sends all echoed messages across all channels back through
/// a single mpsc::Channel to the test. This is a demo of using interposers to
/// verify a complex multi-client flow.
pub struct EchoFactoryInterposer {
    tx: Mutex<mpsc::Sender<String>>,
}

impl EchoFactoryInterposer {
    pub fn new() -> (Arc<EchoFactoryInterposer>, mpsc::Receiver<String>) {
        let (tx, rx) = mpsc::channel(0);
        let tx = Mutex::new(tx);
        (Arc::new(EchoFactoryInterposer { tx }), rx)
    }
}

#[async_trait]
impl Interposer for EchoFactoryInterposer {
    type Marker = fechofactory::EchoFactoryMarker;

    async fn interpose(
        self: Arc<Self>,
        mut from_client: fechofactory::EchoFactoryRequestStream,
        to_service: fechofactory::EchoFactoryProxy,
    ) {
        // Start listening to requests from client
        while let Some(Ok(fechofactory::EchoFactoryRequest::RequestEchoProtocol {
            server_end,
            responder,
        })) = from_client.next().await
        {
            let (interposer, mut rx) = EchoInterposer::new();

            // Create the Interposer <---> Server channel
            let (proxy_to_service, service_server_end) =
                fidl::endpoints::create_proxy::<<EchoInterposer as Interposer>::Marker>()
                    .expect("unable to create endpoints");

            // Forward the request to the service and get a response
            to_service
                .request_echo_protocol(service_server_end)
                .await
                .expect("request_echo_protocol failed");

            fasync::spawn(async move {
                let stream = server_end.into_stream().expect("could not convert into stream");
                interposer.interpose(stream, proxy_to_service).await;
            });

            let mut tx = {
                let tx = self.tx.lock().await;
                tx.clone()
            };
            fasync::spawn(async move {
                while let Some(echo_string) = rx.next().await {
                    tx.send(echo_string).await.expect("local tx/rx channel was closed");
                }
            });

            responder.send().expect("failed to send echo factory response");
        }
    }
}
