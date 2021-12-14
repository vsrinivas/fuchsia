// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_name as fnet_name;
use fuchsia_zircon as zx;

use anyhow::Context as _;
use dns_server_watcher::{DnsServers, DnsServersUpdateSource};
use tracing::trace;

use crate::errors;

/// Updates the DNS servers used by the DNS resolver.
pub(super) async fn update_servers(
    lookup_admin: &fnet_name::LookupAdminProxy,
    dns_servers: &mut DnsServers,
    source: DnsServersUpdateSource,
    servers: Vec<fnet_name::DnsServer_>,
) -> Result<(), errors::Error> {
    trace!("updating DNS servers obtained from {:?} to {:?}", source, servers);

    let () = dns_servers.set_servers_from_source(source, servers);
    let mut servers = dns_servers.consolidated();
    trace!("updating LookupAdmin with DNS servers = {:?}", servers);

    lookup_admin
        .set_dns_servers(&mut servers.iter_mut())
        .await
        .context("error sending set DNS servers request")
        .map_err(errors::Error::NonFatal)?
        .map_err(zx::Status::from_raw)
        .context("error setting DNS servers")
        .map_err(errors::Error::NonFatal)
}
