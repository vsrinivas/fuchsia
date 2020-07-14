// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::marker::Unpin;
use std::pin::Pin;
use std::sync::Arc;

use fidl_fuchsia_net as net;

use futures::sink::Sink;
use futures::task::{Context, Poll};
use futures::SinkExt;
use parking_lot::Mutex;

/// Alias for a list of [`net::SocketAddress`].
///
/// The servers in the list are in priority order.
pub type ServerList = Vec<net::SocketAddress>;

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

    /// Returns the servers.
    pub fn servers(&self) -> ServerList {
        let inner = self.0.lock();
        inner.servers.clone()
    }

    /// Sets the servers after deduplication.
    ///
    /// Returns `false` if the servers did not change.
    fn set_servers(&self, mut servers: ServerList) -> bool {
        let mut set = HashSet::new();
        let () = servers.retain(|s| set.insert(*s));

        let mut inner = self.0.lock();
        if inner.servers == servers {
            return false;
        }

        inner.servers = servers;
        return true;
    }
}

/// A handler for configuring name servers.
///
/// `ServerConfigSink` takes configurations in the form of [`ServerList`]
/// and applies a simple policy to consolidate the configurations into a single
/// list of servers to use when resolving names through DNS:
///   - Any duplicates will be discarded.
///
/// `ServerConfigSink` is instantiated with a [`Sink`] `S` whose `Item` is
/// [`ServerList`]. The `Sink` will receive consolidated configurations
/// sequentially. Every new item received by `S` is a fully assembled
/// [`ServerList`], it may discard any previous configurations it received.
///
/// `ServerConfigSink` itself is a [`Sink`] that takes [`ServerList`] items,
/// consolidates all configurations using the policy described above and
/// forwards the result to `S` if it is different from the current state.
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
    /// Equivalent to [`Sink::send`] with [`ServerList`].
    pub async fn set_servers(
        &mut self,
        servers: impl IntoIterator<Item = net::SocketAddress>,
    ) -> Result<(), ServerConfigSinkError<S::Error>> {
        self.send(servers.into_iter().collect()).await
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

impl<S: Sink<ServerList> + Unpin> Sink<ServerList> for ServerConfigSink<S> {
    type Error = ServerConfigSinkError<S::Error>;

    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Pin::new(&mut self.get_mut().changes_sink)
            .poll_ready(cx)
            .map_err(ServerConfigSinkError::SinkError)
    }

    fn start_send(self: Pin<&mut Self>, item: ServerList) -> Result<(), Self::Error> {
        let me = self.get_mut();
        if !me.state.set_servers(item) {
            return Ok(());
        }

        // Send the conslidated list of servers following the policy (documented
        // on `ServerConfigSink`) to the configurations sink.
        Pin::new(&mut me.changes_sink)
            .start_send(me.state.servers())
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
    use std::convert::TryInto;

    use fidl_fuchsia_net as fnet;
    use fuchsia_async as fasync;

    use futures::future::FutureExt as _;
    use futures::StreamExt;

    use super::*;
    use crate::test_util::*;

    #[test]
    fn test_consolidate() {
        let policy = ServerConfigSink::new(futures::sink::drain());

        let test = |servers: Vec<fnet::SocketAddress>, expected: Vec<fnet::SocketAddress>| {
            policy.state.set_servers(servers);
            assert_eq!(policy.state.servers(), expected);
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
        let (mut src_snd, src_rcv) = futures::channel::mpsc::channel::<ServerList>(1);
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

    #[fasync::run_singlethreaded(test)]
    async fn test_duplicate_update() {
        let (snd, mut rcv) = futures::channel::mpsc::channel::<ServerList>(1);
        let mut policy = ServerConfigSink::new(snd);

        let servers = vec![DHCP_SERVER, NDP_SERVER];
        matches::assert_matches!(policy.send(servers.clone()).await, Ok(()));
        assert_eq!(rcv.next().await.expect("should get servers"), servers);

        // Receiving the same servers in a different order should update the resolver.
        let servers = vec![NDP_SERVER, DHCP_SERVER];
        matches::assert_matches!(policy.send(servers.clone()).await, Ok(()));
        assert_eq!(rcv.next().await.expect("should get servers"), servers);

        // Receiving the same servers again should do nothing.
        matches::assert_matches!(policy.send(servers.clone()).await, Ok(()));
        matches::assert_matches!(rcv.next().now_or_never(), None);

        // Receiving a different list that is the same after deduplication should do nothing.
        matches::assert_matches!(
            policy.send(vec![NDP_SERVER, NDP_SERVER, DHCP_SERVER]).await,
            Ok(())
        );
        matches::assert_matches!(rcv.next().now_or_never(), None);

        // New servers should update the resolver.
        let servers = vec![NDP_SERVER];
        matches::assert_matches!(policy.send(servers.clone()).await, Ok(()));
        assert_eq!(rcv.next().await.expect("should get servers"), servers);
    }
}
