// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_ext as net_ext;
use parking_lot::Mutex;
use std::collections::HashSet;

/// Alias for a list of [`net::SocketAddress`].
///
/// The servers in the list are in priority order.
pub type ServerList = Vec<net::SocketAddress>;

/// Holds current [`ServerConfigSink`] state.
#[derive(Debug)]
struct ServerConfigInner {
    servers: ServerList,
}

/// Provides shared access to a [`ServerList`].
#[derive(Debug)]
pub struct ServerConfigState(Mutex<ServerConfigInner>);

/// The result of updating a [`ServerConfigState`].
#[derive(Debug, PartialEq, Eq)]
pub enum UpdateServersResult {
    /// Server list was updated to the provided value.
    Updated(ServerList),
    /// No change was applied to the server list.
    NoChange,
    /// Invalid servers provided.
    InvalidsServers,
}

impl ServerConfigState {
    /// Creates a new empty `ServerConfigState`.
    pub fn new() -> Self {
        Self(Mutex::new(ServerConfigInner { servers: Vec::new() }))
    }

    /// Returns the servers.
    pub fn servers(&self) -> ServerList {
        let Self(current) = self;
        let inner = current.lock();
        inner.servers.clone()
    }

    /// Updates the server list after deduplication.
    pub fn update_servers(&self, mut servers: ServerList) -> UpdateServersResult {
        let Self(current) = self;

        if servers.iter().any(|s| {
            // Addresses must not contain an unspecified or multicast address.
            let net_ext::SocketAddress(sockaddr) = From::from(*s);
            let ip = sockaddr.ip();
            ip.is_multicast() || ip.is_unspecified()
        }) {
            return UpdateServersResult::InvalidsServers;
        }

        let mut set = HashSet::new();
        let () = servers.retain(|s| set.insert(*s));
        let mut inner = current.lock();
        if inner.servers == servers {
            return UpdateServersResult::NoChange;
        }
        inner.servers = servers.clone();
        UpdateServersResult::Updated(servers)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use net_declare::fidl_socket_addr;

    #[test]
    fn test_config_state() {
        let state = ServerConfigState::new();
        assert_eq!(state.servers(), []);

        // Ordering is respected.
        let values = [DHCP_SERVER, NDP_SERVER];
        assert_eq!(
            state.update_servers(values.to_vec()),
            UpdateServersResult::Updated(values.to_vec())
        );
        assert_eq!(state.servers(), values);

        // Duplicates are removed.
        assert_eq!(
            state.update_servers(vec![DHCP_SERVER, DHCP_SERVER, NDP_SERVER]),
            UpdateServersResult::NoChange
        );
        assert_eq!(state.servers(), values);

        // Bad addresses are rejected.
        for addr in [
            fidl_socket_addr!("0.0.0.0:1111"),
            fidl_socket_addr!("[::]:2222"),
            fidl_socket_addr!("224.0.0.1:3333"),
            fidl_socket_addr!("[ff02::1]:4444"),
        ] {
            assert_eq!(state.update_servers(vec![addr]), UpdateServersResult::InvalidsServers);
        }
        assert_eq!(state.servers(), values);

        // Empty inputs become empty output.
        assert_eq!(state.update_servers(vec![]), UpdateServersResult::Updated(vec![]));
        assert_eq!(state.servers(), []);
    }
}
