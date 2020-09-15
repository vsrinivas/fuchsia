// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For the select macro to expand.
#![recursion_limit = "256"]

mod client;
mod provider;

use {
    anyhow::{Context as _, Error, Result},
    fidl_fuchsia_net_dhcpv6::ClientProviderRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{future, StreamExt, TryStreamExt},
};

enum IncomingService {
    ClientProvider(ClientProviderRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;
    let () = log::info!("starting");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::ClientProvider);
    fs.take_and_serve_directory_handle()?;

    fs.then(future::ok::<_, Error>)
        .try_for_each_concurrent(None, |request| async {
            match request {
                IncomingService::ClientProvider(client_provider_request_stream) => {
                    Ok(provider::run_client_provider(
                        client_provider_request_stream,
                        client::serve_client,
                    )
                    .await)
                }
            }
        })
        .await
}
