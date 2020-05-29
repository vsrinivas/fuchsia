// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    fidl_fuchsia_net_dhcpv6::{
        ClientRequest, ClientRequestStream, ClientWatchServersResponder, OperationalModels,
    },
    futures::TryStreamExt,
};

/// A DHCPv6 client.
#[derive(Default)]
pub(crate) struct Dhcpv6Client {
    dns_responder: Option<ClientWatchServersResponder>,
}

/// Starts a client on `_interface_id` running in `_models`.
///
/// `request_stream` will be serviced by the client.
pub(crate) async fn start_client(
    _interface_id: u64,
    _models: OperationalModels,
    request_stream: ClientRequestStream,
) -> Result<()> {
    // TODO(jayzhuang): handle socket recv and timer.
    request_stream
        .map_err(|err| anyhow!("reading client request from stream: {}", err))
        .try_fold(Dhcpv6Client::default(), |mut client, request| {
            async move {
                match request {
                    ClientRequest::WatchServers { responder } => match client.dns_responder {
                        Some(_) => {
                            // Drop the previous responder to close the channel.
                            client.dns_responder = None;
                            return Err(anyhow!(
                                "got watch request while the previous one is pending"
                            ));
                        }
                        None => client.dns_responder = Some(responder),
                    },
                }
                Ok(client)
            }
        })
        .await
        .map(|_: Dhcpv6Client| ())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_endpoints, fidl_fuchsia_net_dhcpv6::ClientMarker,
        fuchsia_async as fasync, futures::join, matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_client_should_return_error_on_double_watch() {
        let (client_end, server_end) =
            create_endpoints::<ClientMarker>().expect("failed to create test fidl channel");
        let client_proxy = client_end.into_proxy().expect("failed to create test client proxy");
        let client_stream = server_end.into_stream().expect("failed to create test request stream");

        let (caller1_res, caller2_res, client_res) = join!(
            client_proxy.watch_servers(),
            client_proxy.watch_servers(),
            start_client(1, OperationalModels { stateless: None }, client_stream)
        );

        assert_matches!(caller1_res, Err(fidl::Error::ClientChannelClosed(_)));
        assert_matches!(caller2_res, Err(fidl::Error::ClientChannelClosed(_)));
        assert!(client_res
            .expect_err("client should fail with double watch error")
            .to_string()
            .contains("got watch request while the previous one is pending"));
    }
}
