// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_overnet as fovernet,
    fuchsia_component::client::{connect_channel_to_protocol_at_path, connect_to_protocol},
    futures::stream::TryStreamExt,
    tracing::info,
};

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    info!("started");

    let overnet_publisher = connect_to_protocol::<fovernet::ServicePublisherMarker>()?;
    loop {
        let (client, stream) = create_request_stream::<fovernet::ServiceProviderMarker>()?;
        overnet_publisher.publish_service("test.proxy.stress.Stressor", client)?;
        info!("published");
        serve_service_provider(stream).await?;
    }
}

async fn serve_service_provider(
    stream: fovernet::ServiceProviderRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(Error::from)
        .try_for_each_concurrent(None, |request| async move {
            info!("Got request");
            let fovernet::ServiceProviderRequest::ConnectToService { chan, .. } = request;
            connect_channel_to_protocol_at_path(chan, "/svc/test.proxy.stress.Stressor")
                .map_err(Error::from)
        })
        .await
}
