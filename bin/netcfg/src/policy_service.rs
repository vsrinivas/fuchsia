// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::RequestStream,
    fidl_fuchsia_net_policy::{DnsConfig, NetError, PolicyRequest, PolicyRequestStream, Status},
    fidl_fuchsia_netstack::NetstackProxy,
    fuchsia_async as fasync,
    futures::{prelude::*, TryFutureExt},
};

pub fn spawn_netpolicy_fidl_server(netstack: NetstackProxy, channel: fasync::Channel) {
    let stream = PolicyRequestStream::from_channel(channel);
    let fut = serve_fidl_requests(netstack, stream)
        .unwrap_or_else(|e| eprintln!("Error serving netpolicy FIDL call: {}", e));;
    fasync::spawn(fut);
}

async fn serve_fidl_requests(
    netstack: NetstackProxy, stream: PolicyRequestStream,
) -> Result<(), fidl::Error> {
    await!(stream.try_for_each(|req| handle_request(&netstack, req)))
}

async fn handle_request(netstack: &NetstackProxy, req: PolicyRequest) -> Result<(), fidl::Error> {
    match req {
        PolicyRequest::SetDnsConfig { config, responder } => {
            let mut r = set_dns_config(netstack, config);
            responder.send(&mut r)
        }
        PolicyRequest::GetDnsConfig { responder } => {
            let mut r = get_dns_config();
            responder.send(&mut r)
        }
    }
}

fn set_dns_config(netstack: &NetstackProxy, config: DnsConfig) -> NetError {
    let mut parse_results: Vec<fidl_fuchsia_net::IpAddress> =
        config.dns_servers.into_iter().map(Into::into).collect();

    if let Err(e) = netstack.set_name_servers(&mut parse_results.iter_mut()) {
        eprintln!("Error setting DNS server for netstack: {}", e);
        return NetError {
            status: Status::UnknownError,
        };
    }

    NetError { status: Status::Ok }
}

fn get_dns_config() -> DnsConfig {
    // TODO(NET-1430): Add FIDL getters for current netcfg DNS server IP addresses.
    eprintln!("netpolicy: unimplemented get_dns_config called!");
    DnsConfig {
        dns_servers: vec![fidl_fuchsia_net::IpAddress::Ipv4(
            fidl_fuchsia_net::IPv4Address { addr: [0, 0, 0, 0] },
        )],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::endpoints::create_proxy,
        fidl_fuchsia_net_policy::PolicyMarker,
        fidl_fuchsia_netstack::{NetstackMarker, NetstackRequest},
        futures::task::Poll,
        pin_utils::pin_mut,
    };

    fn build_address() -> fidl_fuchsia_net::IpAddress {
        fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::IPv4Address { addr: [1, 1, 1, 1] })
    }

    #[test]
    fn set_dns_test() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        // Set up mock netstack fidl server.
        let (netstack_proxy, netstack_server) =
            create_proxy::<NetstackMarker>().expect("failed to create netstack fidl");
        let mut netstack_stream = netstack_server
            .into_stream()
            .expect("failed to create a request stream.");

        // Set up real netpolicy fidl server and client.
        let (netpolicy_proxy, netpolicy_server) =
            create_proxy::<PolicyMarker>().expect("failed to create netpolicy fidl");

        let netpolicy_stream = netpolicy_server
            .into_stream()
            .expect("failed to create a request stream.");

        let netpolicy_service_task = serve_fidl_requests(netstack_proxy, netpolicy_stream)
            .unwrap_or_else(|e| eprintln!("Error serving netpolicy FIDL call: {}", e));

        // Call netpolicy FIDL call.
        let mut config = DnsConfig {
            dns_servers: vec![build_address()],
        };
        let client_fut = netpolicy_proxy.set_dns_config(&mut config);

        // Let netpolicy client run to stall.
        pin_mut!(client_fut);
        assert!(exec.run_until_stalled(&mut client_fut).is_pending());

        // Let netpolicy server run to stall.
        pin_mut!(netpolicy_service_task);
        assert!(exec
            .run_until_stalled(&mut netpolicy_service_task)
            .is_pending());

        // Let netstack server run to stall and check that we got an appropriate FIDL call.
        let event = match exec.run_until_stalled(&mut netstack_stream.next()) {
            Poll::Ready(Some(Ok(req))) => req,
            _ => panic!("Expected a Netstack fidl call, but there is none!"),
        };

        match event {
            NetstackRequest::SetNameServers {
                servers,
                control_handle: _,
            } => {
                let expected_servers = vec![build_address()];
                assert_eq!(expected_servers, servers);
            }
            _ => panic!("Unexpected netstack call!"),
        };

        // Let netpolicy client run until ready, and check that we got a correct response code.
        let response = match exec.run_until_stalled(&mut client_fut) {
            Poll::Ready(Ok(req)) => req,
            _ => panic!("Expected a response from netpolicy fidl call, but there is none!"),
        };

        assert_eq!(NetError { status: Status::Ok }, response);
    }
}
