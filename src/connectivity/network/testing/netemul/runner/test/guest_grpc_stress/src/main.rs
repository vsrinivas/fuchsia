// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_netemul_guest::{GuestDiscoveryMarker, GuestInteractionMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon::sys,
    netemul_guest_lib::file_to_client,
    std::fs::File,
};

// This test stresses the gRPC client to ensure there are no race conditions between the thread
// that initiates client operations and the thread that processes events from the CompletionQueue.
async fn grpc_client_stress() -> Result<(), Error> {
    // The possible race condition is in the initiation of the client operation.  To ensure the
    // tightest loop possible around that mechanism, query for an invalid path so that the actual
    // operation terminates immediately on the server end.
    let local_file = "/data/local";
    let nonexistent_remote_file = "invalid_path";

    let guest_discovery_service = client::connect_to_service::<GuestDiscoveryMarker>()?;
    let (gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest", gis_ch)?;

    for _ in 0..1000 {
        let get_file = file_to_client(&File::create(local_file)?)?;
        let get_status =
            gis.get_file(nonexistent_remote_file, get_file).await.context("Failed to get file")?;

        match get_status {
            sys::ZX_ERR_NOT_FOUND => {}
            sys::ZX_OK => return Err(format_err!("File transfer erroneously succeeded")),
            error => {
                return Err(format_err!("Failed to transfer file with unexpected error: {}", error))
            }
        }
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    grpc_client_stress().await?;
    return Ok(());
}
