// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_dhcpv6 as fnet_dhcpv6;
use fidl_fuchsia_net_name as fnet_name;

use anyhow::Context as _;
use dns_server_watcher::{DnsServerWatcherEvent, DnsServers, DnsServersUpdateSource};
use futures::future::TryFutureExt as _;
use futures::stream::StreamExt as _;
use log::error;

use crate::{dns, DnsServerWatchers};

/// Start a DHCPv6 client for the specified host interface.
pub(super) fn start_client(
    dhcpv6_client_provider: &fnet_dhcpv6::ClientProviderProxy,
    interface_id: u64,
    sockaddr: fnet::Ipv6SocketAddress,
    watchers: &mut DnsServerWatchers<'_>,
) -> Result<bool, anyhow::Error> {
    let source = DnsServersUpdateSource::Dhcpv6 { interface_id };
    if watchers.contains_key(&source) {
        return Err(anyhow::anyhow!(
            "interface with id={} already has a DHCPv6 client",
            interface_id
        ));
    }

    let params = fnet_dhcpv6::NewClientParams {
        interface_id: Some(interface_id),
        address: Some(sockaddr),
        models: Some(fnet_dhcpv6::OperationalModels {
            stateless: Some(fnet_dhcpv6::Stateless {
                options_to_request: Some(vec![fnet_dhcpv6::RequestableOptionCode::DnsServers]),
            }),
        }),
    };
    let (client, server) = fidl::endpoints::create_proxy::<fnet_dhcpv6::ClientMarker>()
        .context("create DHCPv6 client fidl endpoints")?;
    let () = match dhcpv6_client_provider.new_client(params, server) {
        Ok(o) => o,
        Err(e) => {
            // Not all environments may have a DHCPv6 client service so we consider this a
            // non-fatal error.
            error!(
                "failed to start DHCPv6 client for host interface with id={} using socketaddr {:?}: {}",
                interface_id, sockaddr, e
            );
            return Ok(false);
        }
    };
    let stream = futures::stream::try_unfold(client, move |proxy| {
        proxy
            .watch_servers()
            .map_ok(move |servers| Some((DnsServerWatcherEvent { source, servers }, proxy)))
    })
    .map(move |r| {
        r.with_context(|| {
            format!("DHCPv6 DNS server watcher stream for interface ID = {}", interface_id)
        })
    });

    assert!(
        watchers.insert(source, stream.boxed()).is_none(),
        "interface with id={} should not have a DHCPv6 client",
        interface_id
    );

    Ok(true)
}

/// Stops the DHCPv6 client running on the specified host interface.
///
/// Any DNS servers learned by the client will be cleared.
pub(super) async fn stop_client(
    lookup_admin: &fnet_name::LookupAdminProxy,
    dns_servers: &mut DnsServers,
    interface_id: u64,
    watchers: &mut DnsServerWatchers<'_>,
) -> Result<(), anyhow::Error> {
    let source = DnsServersUpdateSource::Dhcpv6 { interface_id };

    // Dropping the client end of the Client interface should stop the
    // DHCPv6 client.
    if watchers.remove(&source).is_none() {
        // Should never happen as we only set the DHCPv6 client
        // socket address if we successfully create a client.
        return Err(anyhow::anyhow!(
            "expected to remove a DNS watcher for host interface with id={}",
            interface_id
        ));
    }

    dns::update_servers(lookup_admin, dns_servers, source, vec![])
        .await
        .context("clear DNS servers")
}
