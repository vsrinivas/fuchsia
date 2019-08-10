// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::config::ConfigMapValue,
    fidl_fuchsia_boot::{FactoryItemsRequest, FactoryItemsRequestStream},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

type ConfigMap = Arc<RwLock<ConfigMapValue>>;

/// A fake meant for tests which rely on FactoryItems.
pub struct FakeFactoryItemsServer {
    factory_items: ConfigMap,
}
impl FakeFactoryItemsServer {
    /// Creates new fake factory store.
    pub fn new(factory_items: ConfigMap) -> Self {
        Self { factory_items }
    }

    /// Asynchronously handles the supplied stream of `FactoryItemsRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: FactoryItemsRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Asynchronously handles a single `FactoryItemsRequest`.
    async fn handle_request(&self, req: FactoryItemsRequest) -> Result<(), fidl::Error> {
        let FactoryItemsRequest::Get { extra, responder } = req;

        let (payload, length) = match self.factory_items.read().unwrap().get(&extra) {
            Some(item) => {
                let size = item.0.get_size().unwrap();

                (
                    Some(item.0.create_child(zx::VmoChildOptions::COPY_ON_WRITE, 0, size).unwrap()),
                    item.1,
                )
            }
            None => (None, 0),
        };

        responder.send(payload, length)
    }
}

pub fn spawn_fake_factory_items_server(
    server: FakeFactoryItemsServer,
    stream: FactoryItemsRequestStream,
) {
    fasync::spawn(async move {
        server.handle_requests_from_stream(stream).await
            .expect("Failed to run fake_factory_store service")
    });
}
