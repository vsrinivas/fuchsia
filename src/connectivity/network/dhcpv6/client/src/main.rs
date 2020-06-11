// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For the select macro to expand.
#![recursion_limit = "256"]

mod client;
mod provider;

use {
    anyhow::{Error, Result},
    fidl_fuchsia_net_dhcpv6::ClientProviderRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{future, StreamExt, TryStreamExt},
    log::info,
};

enum IncomingService {
    ClientProvider(ClientProviderRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::ClientProvider);
    fs.take_and_serve_directory_handle()?;

    fuchsia_syslog::init_with_tags(&["dhcpv6_client"])?;

    info!("starting DHCPv6 client provider");
    fs.then(future::ok::<_, Error>)
        .try_for_each_concurrent(None, |request| async {
            match request {
                IncomingService::ClientProvider(client_provider_request_stream) => {
                    provider::run_client_provider(
                        client_provider_request_stream,
                        client::start_client,
                    )
                    .await
                }
            }
        })
        .await
}
