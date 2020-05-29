// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides overnet support for network manager.

use {
    crate::fidl_worker::IncomingFidlRequestStream,
    anyhow::{Context as _, Error},
    fidl::endpoints::{create_request_stream, RequestStream, ServiceMarker},
    fidl_fuchsia_overnet::{
        ServiceProviderMarker, ServiceProviderRequest, ServicePublisherMarker,
        ServicePublisherProxy,
    },
    fidl_fuchsia_router_config::{RouterAdminMarker, RouterStateMarker},
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    futures::stream::{self, Stream, StreamExt as _, TryStreamExt as _},
};

/// Returns a `Stream` of [`IncomingFidlRequestStream`]s from overnet.
pub(super) async fn new_stream(
) -> Result<impl Stream<Item = Result<IncomingFidlRequestStream, Error>>, Error> {
    let overnet_svc = fclient::connect_to_service::<ServicePublisherMarker>()?;

    let router_admin_stream = setup_overnet_service::<RouterAdminMarker>(&overnet_svc)
        .await?
        .map_ok(IncomingFidlRequestStream::RouterAdmin);

    let router_state_stream = setup_overnet_service::<RouterStateMarker>(&overnet_svc)
        .await?
        .map_ok(IncomingFidlRequestStream::RouterState);

    Ok(stream::select(router_admin_stream, router_state_stream))
}

/// Publishes the specified service `M` via overnet and returns a `Stream` which yields
/// `M::RequestStream`s as clients connect to the service.
pub(super) async fn setup_overnet_service<M: ServiceMarker>(
    overnet: &ServicePublisherProxy,
) -> Result<impl Stream<Item = Result<M::RequestStream, Error>>, Error> {
    let (client, server) = create_request_stream::<ServiceProviderMarker>()
        .context("failed to create ServiceProvider request stream")?;
    let () = overnet
        .publish_service(&M::NAME, client)
        .with_context(|| format!("failed to publish {} to overnet", &M::NAME))?;

    Ok(server
        .try_filter_map(
            |ServiceProviderRequest::ConnectToService { chan, info: _, control_handle: _ }| async {
                match fasync::Channel::from_channel(chan) {
                    Ok(c) => Ok(Some(M::RequestStream::from_channel(c))),
                    // The client gave us an invalid channel, log the error but do not terminate the
                    // server stream so we handle future requests.
                    Err(e) => {
                        error!(
                            "failed to create fasync::Channel to connect to overnet service {}: {}",
                            &M::NAME,
                            e
                        );
                        Ok(None)
                    }
                }
            },
        )
        .map(|r| {
            r.with_context(|| format!("connection request stream for {} on overnet", M::NAME))
        }))
}
