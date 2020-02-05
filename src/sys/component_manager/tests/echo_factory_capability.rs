// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{breakpoint_system_client::Injector, echo_capability::EchoCapability},
    async_trait::async_trait,
    fidl_fuchsia_test_echofactory as fechofactory, fuchsia_async as fasync,
    futures::StreamExt,
    std::sync::Arc,
};

/// Capability that serves the Echofactory FIDL protocol.
#[derive(Clone)]
pub struct EchoFactoryCapability;

impl EchoFactoryCapability {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub fn serve_async(self: Arc<Self>, request_stream: fechofactory::EchoFactoryRequestStream) {
        fasync::spawn(async move {
            self.serve(request_stream).await;
        });
    }
}

#[async_trait]
impl Injector for EchoFactoryCapability {
    type Marker = fechofactory::EchoFactoryMarker;

    async fn serve(self: Arc<Self>, mut request_stream: fechofactory::EchoFactoryRequestStream) {
        while let Some(Ok(fechofactory::EchoFactoryRequest::RequestEchoProtocol {
            server_end,
            responder,
        })) = request_stream.next().await
        {
            let (capability, mut echo_rx) = EchoCapability::new();
            fasync::spawn(async move {
                let stream = server_end.into_stream().expect("could not convert into stream");
                capability.serve(stream).await;
            });
            fasync::spawn(async move {
                while let Some(echo) = echo_rx.next().await {
                    echo.resume();
                }
            });

            responder.send().expect("failed to send echo factory response");
        }
    }
}
