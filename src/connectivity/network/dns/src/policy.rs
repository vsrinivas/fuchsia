// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_name as name;
use futures::sink::Sink;
use futures::task::{Context, Poll};
use futures::SinkExt;
use parking_lot::Mutex;
use std::collections::HashSet;
use std::convert::TryFrom;
use std::marker::Unpin;
use std::pin::Pin;
use std::sync::Arc;

/// Alias for a list of [`Server`].
///
/// The servers in the list are in priority order.
pub type ServerList = Vec<Server>;

/// A DNS server.
///
/// `Server` is equivalent to [`name::DnsServer_`] where the `Option` fields are
/// realized. That gives us more type-safety when dealing with server
/// configurations, consolidating them into a valid `Server` provided by the
/// `TryFrom<name::DnsServer_>` impl.
#[derive(PartialEq, Debug)]
pub struct Server {
    /// The socket address where the server can be reached.
    pub address: net::SocketAddress,
    /// The configuration source.
    pub source: name::DnsServerSource,
}

impl Clone for Server {
    // NOTE: name::DnsServerSource does not derive `Clone`, we need to implement
    // it manually.
    fn clone(&self) -> Self {
        Self {
            address: self.address.clone(),
            source: match &self.source {
                name::DnsServerSource::StaticSource(_satic_source) => {
                    name::DnsServerSource::StaticSource(name::StaticDnsServerSource {})
                }
                name::DnsServerSource::Dhcp(dhcp) => {
                    name::DnsServerSource::Dhcp(name::DhcpDnsServerSource {
                        source_interface: dhcp.source_interface.clone(),
                    })
                }
                name::DnsServerSource::Ndp(ndp) => {
                    name::DnsServerSource::Ndp(name::NdpDnsServerSource {
                        source_interface: ndp.source_interface.clone(),
                    })
                }
                name::DnsServerSource::Dhcpv6(dhcpv6) => {
                    name::DnsServerSource::Dhcpv6(name::Dhcpv6DnsServerSource {
                        source_interface: dhcpv6.source_interface.clone(),
                    })
                }
            },
        }
    }
}

impl From<Server> for name::DnsServer_ {
    fn from(s: Server) -> name::DnsServer_ {
        name::DnsServer_ { address: Some(s.address), source: Some(s.source) }
    }
}

#[derive(Debug, Copy, Clone)]
pub struct MissingAddressError;

impl TryFrom<name::DnsServer_> for Server {
    type Error = MissingAddressError;

    fn try_from(fidl: name::DnsServer_) -> Result<Self, Self::Error> {
        let name::DnsServer_ { address, source } = fidl;
        match address {
            Some(address) => Ok(Server {
                address,
                source: source
                    .unwrap_or(name::DnsServerSource::StaticSource(name::StaticDnsServerSource {})),
            }),
            None => Err(MissingAddressError),
        }
    }
}

/// The list of DNS servers used in [`ServerConfigSink`].
pub type DnsServersListConfig = Vec<name::DnsServer_>;

/// Holds current [`ServerConfigSink`] state.
#[derive(Debug)]
struct ServerConfigInner {
    servers: ServerList,
}

/// Provides shared access to [`ServerConfigSink`]'s state.
#[derive(Debug)]
pub struct ServerConfigState(Mutex<ServerConfigInner>);

impl ServerConfigState {
    /// Creates a new empty `ServerConfigState`.
    pub fn new() -> Self {
        Self(Mutex::new(ServerConfigInner { servers: Vec::new() }))
    }

    /// Sets the servers.
    fn set_servers(&self, servers: impl IntoIterator<Item = Server>) {
        self.0.lock().servers = servers.into_iter().collect();
    }

    /// Consolidates the current configuration into a vector of [`Server`]s in
    /// priority order.
    pub fn consolidate(&self) -> ServerList {
        self.consolidate_map(|s| s.clone())
    }

    /// Consolidates the current configuration applying a mapping function `f`
    /// to every item and returns the collected `Vec`.
    pub fn consolidate_map<T: 'static, F: Fn(&Server) -> T>(&self, f: F) -> Vec<T> {
        let mut set = HashSet::new();
        let inner = self.0.lock();
        inner.servers.iter().filter(move |s| set.insert(s.address)).map(f).collect()
    }
}

/// A handler for configuring name servers.
///
/// `ServerConfigSink` takes configurations in the form of
/// [`ServerConfiguration`] and applies a simple policy to consolidate the
/// configurations into a single list of servers to use when resolving names
/// through DNS:
///   - Any duplicates will be discarded.
///
/// `ServerConfigSink` is instantiated with a [`Sink`] `S` whose `Item` is
/// [`ServerList`]. The `Sink` will receive consolidated configurations
/// sequentially. Every new item received by `S` is a fully assembled
/// [`ServerList`], it may discard any previous configurations it received.
///
/// `ServerConfigSink` itself is a [`Sink`] that takes [`ServerConfiguration`]
/// items, consolidates all configurations using the policy described above and
/// forwards the result to `S`.
pub struct ServerConfigSink<S> {
    state: Arc<ServerConfigState>,
    changes_sink: S,
}

impl<S> Unpin for ServerConfigSink<S> where S: Unpin {}

impl<S: Sink<ServerList> + Unpin> ServerConfigSink<S> {
    /// Creates a new [`ServerConfigSink`] that provides consolidated
    /// [`ServerList`]s to `changes_sink`.
    pub fn new(changes_sink: S) -> Self {
        Self::new_with_state(changes_sink, Arc::new(ServerConfigState::new()))
    }

    /// Creates a new [`ServerConfigSink`] with the provided `initial_state`.
    ///
    /// NOTE: `state` will not be reported to `changes_sink`.
    pub fn new_with_state(changes_sink: S, initial_state: Arc<ServerConfigState>) -> Self {
        Self { changes_sink, state: initial_state }
    }

    /// Shorthand to update the servers.
    ///
    /// Equivalent to [`Sink::send`] with  [`ServerConfig`].
    pub async fn set_servers(
        &mut self,
        servers: impl IntoIterator<Item = Server>,
    ) -> Result<(), ServerConfigSinkError<S::Error>> {
        self.send(servers.into_iter().map(Into::into).collect()).await
    }

    /// Gets a [`ServerConfigState`] which provides shared access to this
    /// [`ServerConfigSink`]'s internal state.
    pub fn state(&self) -> Arc<ServerConfigState> {
        self.state.clone()
    }
}

#[derive(Debug)]
pub enum ServerConfigSinkError<E> {
    InvalidArg,
    SinkError(E),
}

impl<S: Sink<ServerList> + Unpin> Sink<DnsServersListConfig> for ServerConfigSink<S> {
    type Error = ServerConfigSinkError<S::Error>;

    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_ready(cx)
            .map_err(ServerConfigSinkError::SinkError)
    }

    fn start_send(self: Pin<&mut Self>, item: DnsServersListConfig) -> Result<(), Self::Error> {
        let me = self.get_mut();
        me.state.set_servers(
            item.into_iter()
                .try_fold(Vec::new(), |mut acc, s| {
                    acc.push(Server::try_from(s)?);
                    Ok(acc)
                })
                .map_err(|MissingAddressError {}| ServerConfigSinkError::InvalidArg)?,
        );

        Pin::new(&mut me.changes_sink)
            .start_send(me.state.consolidate())
            .map_err(ServerConfigSinkError::SinkError)
    }

    fn poll_flush(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_flush(cx)
            .map_err(ServerConfigSinkError::SinkError)
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_close(cx)
            .map_err(ServerConfigSinkError::SinkError)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fidl_fuchsia_net_name as fname;
    use fuchsia_async as fasync;
    use futures::StreamExt;
    use std::convert::TryInto;

    fn to_static_dns_server_list(l: impl IntoIterator<Item = fname::DnsServer_>) -> ServerList {
        l.into_iter().map(|s| s.try_into().unwrap()).collect()
    }

    #[test]
    fn test_consolidate() {
        let policy = ServerConfigSink::new(futures::sink::drain());

        let test = |servers: Vec<fname::DnsServer_>, expected: Vec<fname::DnsServer_>| {
            policy.state.set_servers(to_static_dns_server_list(servers));
            assert_eq!(policy.state.consolidate(), to_static_dns_server_list(expected));
        };

        // Empty inputs become empty output.
        test(vec![], vec![]);

        // Empty ordering is respected.
        test(vec![DHCP_SERVER, NDP_SERVER], vec![DHCP_SERVER, NDP_SERVER]);

        // Duplicates are removed.
        test(vec![DHCP_SERVER, DHCP_SERVER, NDP_SERVER], vec![DHCP_SERVER, NDP_SERVER]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_configuration_sink() {
        let (mut src_snd, src_rcv) = futures::channel::mpsc::channel::<DnsServersListConfig>(1);
        let (dst_snd, mut dst_rcv) = futures::channel::mpsc::channel::<ServerList>(1);
        let policy = ServerConfigSink::new(dst_snd);

        let combined = src_rcv.map(Result::Ok).forward(policy);

        let (combined_result, mut dst_rcv) = futures::future::join(combined, async move {
            // Set a server.
            let () = src_snd.send(vec![DHCPV6_SERVER]).await.expect("Failed to send message");

            let config = dst_rcv.next().await.expect("Destination stream shouldn't end");

            assert_eq!(config, vec![DHCPV6_SERVER.try_into().unwrap()]);

            dst_rcv
        })
        .await;
        let () = combined_result.expect("Sink forwarding failed");
        assert_eq!(None, dst_rcv.next().await, "Configuration sink must have reached end");
    }
}
