// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The prototype mod contains implementations of prototype features for
//! account manager which are not ready for general use and should be disabled
//! by default.  At the moment, this contains only a prototype for account
//! transfer.

mod account_manager_peer;
mod account_transfer_control;

use account_manager_peer::AccountManagerPeer;
use account_transfer_control::AccountTransferControl;
use anyhow::Error;
use fidl::endpoints::{create_request_stream, RequestStream, ServiceMarker};
use fidl_fuchsia_identity_transfer::{AccountManagerPeerMarker, AccountManagerPeerRequestStream};
use fidl_fuchsia_overnet::{
    OvernetMarker, ServiceProviderMarker, ServiceProviderRequest, ServiceProviderRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::server::{ServiceFs, ServiceObj};
use futures::prelude::*;
use log::{error, info};
use std::sync::Arc;

/// Publishes the `PrototypeAccountTransferControl` protocol to the debug
/// output directory.
pub fn publish_account_transfer_control(fs: &mut ServiceFs<ServiceObj<'_, ()>>) {
    let transfer_control = Arc::new(AccountTransferControl::new());
    fs.dir("svc").dir("debug").add_fidl_service(move |stream| {
        let transfer_control_clone = Arc::clone(&transfer_control);
        fasync::spawn(async move {
            transfer_control_clone
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Error handling AccountTransferControl channel {:?}", e))
        });
    });
}

/// Publishes the `AccountManagerPeer` interface to public Overnet.
pub fn publish_account_manager_peer_to_overnet() -> Result<(), Error> {
    let overnet = connect_to_service::<OvernetMarker>()?;
    let (client, stream) = create_request_stream::<ServiceProviderMarker>()?;
    overnet.publish_service(AccountManagerPeerMarker::NAME, client)?;

    fasync::spawn(async move {
        handle_overnet_connection_requests(stream)
            .await
            .unwrap_or_else(|e| error!("Error serving Overnet ServiceProvider: {:?}", e));
    });
    Ok(())
}

/// Serves connection requests to AccountManagerPeer through Overnet.
async fn handle_overnet_connection_requests(
    mut stream: ServiceProviderRequestStream,
) -> Result<(), Error> {
    info!("Serving requests for AccountManagerPeer");
    let account_manager_peer = Arc::new(AccountManagerPeer::new());
    while let Some(ServiceProviderRequest::ConnectToService { chan, .. }) =
        stream.try_next().await?
    {
        info!("Received an AccountManagerPeer connection request");
        let account_manager_peer_clone = Arc::clone(&account_manager_peer);
        let chan = fasync::Channel::from_channel(chan)?;
        let stream = AccountManagerPeerRequestStream::from_channel(chan);
        fasync::spawn(async move {
            account_manager_peer_clone
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Error serving AccountManagerPeer: {:?}", e));
        });
    }
    Ok(())
}
