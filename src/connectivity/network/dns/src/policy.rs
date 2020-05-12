// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::DEFAULT_PORT;
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

/// A piece of configuration used in [`ServerConfigPolicy`].
pub enum ServerConfig {
    /// A list of default servers specified only by their IP addresses.
    ///
    /// The specified servers are assumed to operate on [`DEFAULT_PORT`] and not
    /// bound to any devices.
    DefaultServers(Vec<net::IpAddress>),
    /// A list of dynamically discovered servers.
    ///
    /// Dynamically discovered servers take precedence over default servers in
    /// the consolidated list provided by [`ServerConfigPolicy`].
    DynamicServers(Vec<name::DnsServer_>),
}

/// Holds current [`ServerConfigPolicy`] state.
#[derive(Debug)]
struct ServerConfigInner {
    default_servers: ServerList,
    dynamic_servers: ServerList,
}

/// Provides shared access to [`ServerConfigPolicy`]'s state.
#[derive(Debug)]
pub struct ServerConfigState(Mutex<ServerConfigInner>);

impl ServerConfigState {
    /// Creates a new empty `ServerConfigState`.
    pub fn new() -> Self {
        Self(Mutex::new(ServerConfigInner {
            default_servers: Vec::new(),
            dynamic_servers: Vec::new(),
        }))
    }

    /// Sets the default servers.
    fn set_default_servers(&self, default_servers: impl IntoIterator<Item = Server>) {
        self.0.lock().default_servers = default_servers.into_iter().collect();
    }

    /// Sets the dynamic servers.
    fn set_dynamic_servers(&self, dynamic_servers: impl IntoIterator<Item = Server>) {
        self.0.lock().dynamic_servers = dynamic_servers.into_iter().collect();
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
        inner
            .dynamic_servers
            .iter()
            .chain(inner.default_servers.iter())
            .filter(move |s| set.insert(s.address))
            .map(f)
            .collect()
    }
}

/// A handler for configuring name servers.
///
/// `ServerConfigPolicy` takes configurations in the form of
/// [`ServerConfiguration`] and applies a simple policy to consolidate the
/// configurations into a single list of servers to use when resolving names
/// through DNS:
///   - [`ServerConfiguration::DynamicServers`] have higher priority.
///   - [`ServerConfiguration::DefaultServers`] have lower priority.
///   - Any duplicates will be discarded.
///
/// `ServerConfigPolicy` is instantiated with a [`Sink`] `S` whose `Item` is
/// [`ServerList`]. The `Sink` will receive consolidated configurations
/// sequentially. Every new item received by `S` is a fully assembled
/// [`ServerList`], it may discard any previous configurations it received.
///
/// `ServerConfigPolicy` itself is a [`Sink`] that takes [`ServerConfiguration`]
/// items, consolidates all configurations using the policy described above and
/// forwards the result to `S`.
pub struct ServerConfigPolicy<S> {
    state: Arc<ServerConfigState>,
    changes_sink: S,
}

fn map_ip_to_default_socket_address(ip: net::IpAddress) -> net::SocketAddress {
    match ip {
        net::IpAddress::Ipv4(ip) => {
            net::SocketAddress::Ipv4(net::Ipv4SocketAddress { address: ip, port: DEFAULT_PORT })
        }
        net::IpAddress::Ipv6(ip) => net::SocketAddress::Ipv6(net::Ipv6SocketAddress {
            address: ip,
            port: DEFAULT_PORT,
            zone_index: 0,
        }),
    }
}

impl<S> Unpin for ServerConfigPolicy<S> where S: Unpin {}

impl<S: Sink<ServerList> + Unpin> ServerConfigPolicy<S> {
    /// Creates a new [`ServerConfigPolicy`] that provides consolidated
    /// [`ServerList`]s to `changes_sink`.
    pub fn new(changes_sink: S) -> Self {
        Self::new_with_state(changes_sink, Arc::new(ServerConfigState::new()))
    }

    /// Creates a new [`ServerConfigPolicy`] with the provided `initial_state`.
    ///
    /// NOTE: `state` will not be reported to `changes_sink`.
    pub fn new_with_state(changes_sink: S, initial_state: Arc<ServerConfigState>) -> Self {
        Self { changes_sink, state: initial_state }
    }

    /// Shorthand to update the default servers.
    ///
    /// Equivalent to [`Sink::send`] with  [`ServerConfiguration::DefaultServers`].
    pub async fn set_default_servers(
        &mut self,
        default_servers: impl IntoIterator<Item = net::IpAddress>,
    ) -> Result<(), ServerConfigPolicyError<S::Error>> {
        self.send(ServerConfig::DefaultServers(default_servers.into_iter().collect())).await
    }

    /// Shorthand to update the dynamic servers.
    ///
    /// Equivalent to [`Sink::send`] with  [`ServerConfiguration::DynamicServers`].
    pub async fn set_dynamic_servers(
        &mut self,
        dynamic_servers: impl IntoIterator<Item = Server>,
    ) -> Result<(), ServerConfigPolicyError<S::Error>> {
        self.send(ServerConfig::DynamicServers(
            dynamic_servers.into_iter().map(Into::into).collect(),
        ))
        .await
    }

    /// Gets a [`ServerConfigState`] which provides shared access to this
    /// [`ServerConfigPolicy`]'s internal state.
    pub fn state(&self) -> Arc<ServerConfigState> {
        self.state.clone()
    }
}

#[derive(Debug)]
pub enum ServerConfigPolicyError<E> {
    InvalidArg,
    SinkError(E),
}

impl<S: Sink<ServerList> + Unpin> Sink<ServerConfig> for ServerConfigPolicy<S> {
    type Error = ServerConfigPolicyError<S::Error>;

    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_ready(cx)
            .map_err(ServerConfigPolicyError::SinkError)
    }

    fn start_send(self: Pin<&mut Self>, item: ServerConfig) -> Result<(), Self::Error> {
        let me = self.get_mut();
        match item {
            ServerConfig::DefaultServers(srv) => {
                me.state.set_default_servers(srv.into_iter().map(|s| Server {
                    address: map_ip_to_default_socket_address(s),
                    source: name::DnsServerSource::StaticSource(name::StaticDnsServerSource {}),
                }));
            }
            ServerConfig::DynamicServers(srv) => me.state.set_dynamic_servers(
                srv.into_iter()
                    .try_fold(Vec::new(), |mut acc, s| {
                        acc.push(Server::try_from(s)?);
                        Ok(acc)
                    })
                    .map_err(|MissingAddressError {}| ServerConfigPolicyError::InvalidArg)?,
            ),
        }

        Pin::new(&mut me.changes_sink)
            .start_send(me.state.consolidate())
            .map_err(ServerConfigPolicyError::SinkError)
    }

    fn poll_flush(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_flush(cx)
            .map_err(ServerConfigPolicyError::SinkError)
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_close(cx)
            .map_err(ServerConfigPolicyError::SinkError)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fuchsia_async as fasync;
    use futures::StreamExt;
    use std::convert::TryInto;

    fn to_static_dns_server_list(l: impl IntoIterator<Item = net::SocketAddress>) -> ServerList {
        l.into_iter().map(|s| to_static_server(s).try_into().unwrap()).collect()
    }

    #[test]
    fn test_consolidate() {
        let policy = ServerConfigPolicy::new(futures::sink::drain());

        let test = |default: Vec<net::SocketAddress>,
                    dynamic: Vec<net::SocketAddress>,
                    expected: Vec<net::SocketAddress>| {
            policy.state.set_default_servers(to_static_dns_server_list(default));
            policy.state.set_dynamic_servers(to_static_dns_server_list(dynamic));
            assert_eq!(policy.state.consolidate(), to_static_dns_server_list(expected));
        };

        // Empty inputs become empty output.
        test(vec![], vec![], vec![]);

        // Empty ordering is respected.
        test(
            vec![DEFAULT_SERVER_A, DEFAULT_SERVER_B],
            vec![DYNAMIC_SERVER_A, DYNAMIC_SERVER_B],
            vec![DYNAMIC_SERVER_A, DYNAMIC_SERVER_B, DEFAULT_SERVER_A, DEFAULT_SERVER_B],
        );

        // Duplicates are removed.
        test(
            vec![DEFAULT_SERVER_A],
            vec![DYNAMIC_SERVER_A, DEFAULT_SERVER_A],
            vec![DYNAMIC_SERVER_A, DEFAULT_SERVER_A],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_configuration_sink() {
        let (mut src_snd, src_rcv) = futures::channel::mpsc::channel::<ServerConfig>(1);
        let (dst_snd, mut dst_rcv) = futures::channel::mpsc::channel::<ServerList>(1);
        let policy = ServerConfigPolicy::new(dst_snd);

        let combined = src_rcv.map(Result::Ok).forward(policy);

        let (combined_result, mut dst_rcv) = futures::future::join(combined, async move {
            // Set some default servers.
            let () = src_snd
                .send(ServerConfig::DefaultServers(vec![
                    get_server_address(DEFAULT_SERVER_A),
                    get_server_address(DEFAULT_SERVER_B),
                ]))
                .await
                .expect("Failed to send message");

            let config = dst_rcv.next().await.expect("Destination stream shouldn't end");

            assert_eq!(config, to_static_dns_server_list(vec![DEFAULT_SERVER_A, DEFAULT_SERVER_B]));

            // Set a dynamic server.
            let () = src_snd
                .send(ServerConfig::DynamicServers(vec![to_discovered_server(DYNAMIC_SERVER_A)
                    .try_into()
                    .unwrap()]))
                .await
                .expect("Failed to send message");

            let config = dst_rcv.next().await.expect("Destination stream shouldn't end");

            assert_eq!(
                config,
                vec![
                    to_discovered_server(DYNAMIC_SERVER_A).try_into().unwrap(),
                    to_static_server(DEFAULT_SERVER_A).try_into().unwrap(),
                    to_static_server(DEFAULT_SERVER_B).try_into().unwrap()
                ]
            );

            // Change the default servers.
            let () = src_snd
                .send(ServerConfig::DefaultServers(vec![get_server_address(DEFAULT_SERVER_A)]))
                .await
                .expect("Failed to send message");

            let config = dst_rcv.next().await.expect("Destination stream shouldn't end");

            assert_eq!(
                config,
                vec![
                    to_discovered_server(DYNAMIC_SERVER_A).try_into().unwrap(),
                    to_static_server(DEFAULT_SERVER_A).try_into().unwrap()
                ]
            );

            dst_rcv
        })
        .await;
        let () = combined_result.expect("Sink forwarding failed");
        assert_eq!(None, dst_rcv.next().await, "Configuration sink must have reached end");
    }
}
