// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_netemul_guest::{GuestDiscoveryMarker, GuestInteractionMarker},
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{StreamExt as _, TryStreamExt as _},
};

// This test stresses the gRPC client to ensure there are no race conditions between the thread
// that initiates client operations and the thread that processes events from the CompletionQueue.
async fn grpc_client_stress() -> Result<(), Error> {
    // The possible race condition is in the initiation of the client operation.  To ensure the
    // tightest loop possible around that mechanism, query for an invalid path so that the actual
    // operation terminates immediately on the server end.
    const DESTINATION_LOCAL_FILE: &str = "/cache/destination";
    const NONEXISTENT_REMOTE_FILE: &str = "invalid_path";

    let guest_discovery_service = client::connect_to_protocol::<GuestDiscoveryMarker>()?;
    let (gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest", gis_ch)?;

    let file = std::fs::File::create(DESTINATION_LOCAL_FILE)?;

    futures::stream::iter(0..1000)
        .map(Ok)
        .try_for_each_concurrent(None, move |i| {
            let get_file = netemul_guest_lib::file_to_client(&file)
                .context("Failed to get client")
                .map(|client_end| gis.get_file(NONEXISTENT_REMOTE_FILE, client_end));
            async move {
                let get_file = get_file?;
                println!("[START] get_file i={}", i);
                let get_status = get_file.await.context("Failed to get file")?;
                println!("[END] get_file i={}", i);
                match get_status {
                    zx::sys::ZX_ERR_NOT_FOUND => Ok(()),
                    zx::sys::ZX_OK => Err(format_err!("File transfer erroneously succeeded")),
                    error => Err(format_err!(
                        "Failed to transfer file with unexpected error: {:?}",
                        error
                    )),
                }
            }
        })
        .await
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("[START] grpc_client_stress");
    let () = grpc_client_stress().await?;
    println!("[END] grpc_client_stress");

    Ok(())
}
