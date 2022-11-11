// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_bluetooth_fastpair::{
    ProviderMarker, ProviderWatcherMarker, ProviderWatcherRequestStream,
};
use fidl_fuchsia_bluetooth_sys::{
    InputCapability, OutputCapability, PairingDelegateMarker, PairingDelegateRequest,
    PairingDelegateRequestStream, PairingMarker,
};
use fuchsia_component::client::connect_to_protocol;
use futures::{pin_mut, select, stream::TryStreamExt, FutureExt};
use tracing::{info, warn};

async fn process_provider_events(mut stream: ProviderWatcherRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        let (id, responder) = request.into_on_pairing_complete().expect("only one method");
        let _ = responder.send();
        info!(?id, "Successful Fast Pair pairing");
    }
    info!("Provider service ended");
    Ok(())
}

async fn process_pairing_events(mut stream: PairingDelegateRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            PairingDelegateRequest::OnPairingRequest {
                peer,
                method,
                displayed_passkey,
                responder,
            } => {
                info!("Received pairing request for peer: {:?} with method: {:?}", peer, method);
                // Accept all "normal" pairing requests.
                let _ = responder.send(true, displayed_passkey);
            }
            PairingDelegateRequest::OnPairingComplete { id, success, .. } => {
                info!(?id, "Normal pairing complete (success = {})", success);
            }
            _ => {}
        }
    }
    info!("Pairing Delegate service ended");
    Ok(())
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let fast_pair_svc = connect_to_protocol::<ProviderMarker>()
        .context("failed to connect to `fastpair.Provider` service")?;
    let pairing_svc = connect_to_protocol::<PairingMarker>()
        .context("failed to connect to `sys.Pairing` service")?;
    let (fastpair_client, fastpair_server) = create_request_stream::<ProviderWatcherMarker>()?;

    if let Err(e) = fast_pair_svc.enable(fastpair_client).await {
        warn!("Couldn't enable Fast Pair Provider service: {:?}", e);
        return Ok(());
    }
    info!("Enabled Fast Pair Provider");

    let (pairing_client, pairing_server) = create_request_stream::<PairingDelegateMarker>()?;
    if let Err(e) = pairing_svc.set_pairing_delegate(
        InputCapability::None,
        OutputCapability::None,
        pairing_client,
    ) {
        warn!("Couldn't enable Pairing service: {:?}", e);
        return Ok(());
    }
    info!("Enabled Pairing service");

    let pairing_fut = process_pairing_events(pairing_server).fuse();
    let fastpair_fut = process_provider_events(fastpair_server).fuse();
    pin_mut!(pairing_fut, fastpair_fut);

    select! {
        pairing_result = pairing_fut => info!("Pairing service unexpectedly finished: {:?}", pairing_result),
        fastpair_result = fastpair_fut => info!("Fast Pair service unexpectedly finished: {:?}", fastpair_result),
    }
    Ok(())
}
