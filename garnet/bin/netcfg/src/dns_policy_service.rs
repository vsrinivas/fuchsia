// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net_dns::{DnsConfig, DnsPolicyRequest, DnsPolicyRequestStream, NetError, Status},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::{prelude::*, TryFutureExt},
};

pub fn spawn_net_dns_fidl_server(
    resolver_admin: fidl_fuchsia_netstack::ResolverAdminProxy,
    stream: DnsPolicyRequestStream,
) {
    let fut = serve_fidl_requests(resolver_admin, stream)
        .unwrap_or_else(|e| fx_log_err!("failed to serve net dns FIDL call: {}", e));
    fasync::spawn(fut);
}

async fn serve_fidl_requests(
    resolver_admin: fidl_fuchsia_netstack::ResolverAdminProxy,
    stream: DnsPolicyRequestStream,
) -> Result<(), fidl::Error> {
    await!(stream.try_for_each(|req| handle_request(&resolver_admin, req)))
}

async fn handle_request(
    resolver_admin: &fidl_fuchsia_netstack::ResolverAdminProxy,
    req: DnsPolicyRequest,
) -> Result<(), fidl::Error> {
    match req {
        DnsPolicyRequest::SetDnsConfig { config, responder } => {
            let mut r = set_dns_config(resolver_admin, config);
            responder.send(&mut r)
        }
        DnsPolicyRequest::GetDnsConfig { responder } => {
            let mut r = get_dns_config();
            responder.send(&mut r)
        }
    }
}

fn set_dns_config(
    resolver_admin: &fidl_fuchsia_netstack::ResolverAdminProxy,
    config: DnsConfig,
) -> NetError {
    let mut parse_results: Vec<fidl_fuchsia_net::IpAddress> =
        config.dns_servers.into_iter().map(Into::into).collect();

    if let Err(e) = resolver_admin.set_name_servers(&mut parse_results.iter_mut()) {
        fx_log_err!("failed to set DNS server: {}", e);
        return NetError { status: Status::UnknownError };
    }

    NetError { status: Status::Ok }
}

fn get_dns_config() -> DnsConfig {
    // TODO(NET-1430): Add FIDL getters for current netcfg DNS server IP addresses.
    fx_log_err!("netdns: unimplemented get_dns_config called!");
    DnsConfig {
        dns_servers: vec![fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
            addr: [0, 0, 0, 0],
        })],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::endpoints::create_proxy, fidl_fuchsia_net_dns::DnsPolicyMarker, futures::task::Poll,
        pin_utils::pin_mut,
    };

    fn build_address() -> fidl_fuchsia_net::IpAddress {
        fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr: [1, 1, 1, 1] })
    }

    #[test]
    fn set_dns_test() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        // Set up mock resolver_admin fidl server.
        let (resolver_admin_proxy, resolver_admin_server) =
            create_proxy::<fidl_fuchsia_netstack::ResolverAdminMarker>()
                .expect("failed to create resolver_admin fidl");
        let mut resolver_admin_stream =
            resolver_admin_server.into_stream().expect("failed to create a request stream.");

        // Set up real dns fidl server and client.
        let (dnspolicy_proxy, dnspolicy_server) =
            create_proxy::<DnsPolicyMarker>().expect("failed to create dnspolicy fidl");

        let dnspolicy_stream =
            dnspolicy_server.into_stream().expect("failed to create a request stream.");

        let dnspolicy_service_task = serve_fidl_requests(resolver_admin_proxy, dnspolicy_stream)
            .unwrap_or_else(|e| fx_log_err!("failed to serve dnspolicy FIDL call: {}", e));

        // Call dnspolicy FIDL call.
        let mut config = DnsConfig { dns_servers: vec![build_address()] };
        let client_fut = dnspolicy_proxy.set_dns_config(&mut config);

        // Let dnspolicy client run to stall.
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_pending());

        // Let dnspolicy server run to stall.
        pin_mut!(dnspolicy_service_task);
        assert!(exec.run_until_stalled(&mut dnspolicy_service_task).is_pending());

        // Let resolver_admin server run to stall and check that we got an appropriate FIDL call.
        let event = match exec.run_until_stalled(&mut resolver_admin_stream.next()) {
            Poll::Ready(Some(Ok(req))) => req,
            _ => panic!("Expected a Netstack fidl call, but there is none!"),
        };

        match event {
            fidl_fuchsia_netstack::ResolverAdminRequest::SetNameServers {
                servers,
                control_handle: _,
            } => {
                let expected_servers = vec![build_address()];
                assert_eq!(expected_servers, servers);
            }
        };

        // Let dnspolicy client run until ready, and check that we got a correct response code.
        let response = match exec.run_until_stalled(&mut client_fut) {
            Poll::Ready(Ok(req)) => req,
            _ => panic!("Expected a response from dnspolicy fidl call, but there is none!"),
        };

        assert_eq!(NetError { status: Status::Ok }, response);
    }
}
