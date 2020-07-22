// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{echo_capability::EchoCapability, events::Injector},
    anyhow::Error,
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
        fasync::Task::spawn(async move {
            self.serve(request_stream).await.expect("EchoFactoryCapability injector failed");
        })
        .detach();
    }
}

#[async_trait]
impl Injector for EchoFactoryCapability {
    type Marker = fechofactory::EchoFactoryMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: fechofactory::EchoFactoryRequestStream,
    ) -> Result<(), Error> {
        while let Some(Ok(fechofactory::EchoFactoryRequest::RequestEchoProtocol {
            server_end,
            responder,
        })) = request_stream.next().await
        {
            let (capability, mut echo_rx) = EchoCapability::new();
            fasync::Task::spawn(async move {
                let stream = server_end.into_stream().expect("could not convert into stream");
                capability.serve(stream).await.expect("EchoCapability injector failed");
            })
            .detach();
            fasync::Task::spawn(async move {
                while let Some(echo) = echo_rx.next().await {
                    echo.resume();
                }
            })
            .detach();

            responder.send()?;
        }
        Ok(())
    }
}
