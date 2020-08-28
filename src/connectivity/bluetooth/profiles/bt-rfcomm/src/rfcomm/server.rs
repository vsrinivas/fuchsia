// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_bredr::ConnectionReceiverProxy,
    log::info,
    std::collections::{HashMap, HashSet},
};

use crate::rfcomm::ServerChannel;

/// The RfcommServer handles connection requests from local clients and remote
/// peers.
/// It is responsible for allocating local server channels, managing RFCOMM Sessions
/// between us and remote peers, and relaying RFCOMM connections to local clients.
pub struct RfcommServer {
    /// The local RFCOMM Server Channels that have been assigned to clients.
    /// A client is represented as a ConnectionReceiverProxy.
    server_channels: HashMap<ServerChannel, ConnectionReceiverProxy>,
}

impl RfcommServer {
    pub fn new() -> Self {
        Self { server_channels: HashMap::new() }
    }

    /// Returns the number of available server channels in this server.
    pub fn available_server_channels(&self) -> usize {
        ServerChannel::all().filter(|sc| !self.server_channels.contains_key(&sc)).count()
    }

    /// De-allocates the server `channel`.
    fn free_server_channel(&mut self, channel: &ServerChannel) {
        self.server_channels.remove(channel);
    }

    /// De-allocates the server `channels` provided.
    pub fn free_server_channels(&mut self, channels: &HashSet<ServerChannel>) {
        for sc in channels {
            self.free_server_channel(sc);
        }
    }

    /// De-allocates all the server channels in this server.
    pub fn free_all_server_channels(&mut self) {
        self.server_channels.clear();
    }

    /// Reserves the next available ServerChannel for a client's `proxy`.
    ///
    /// Returns the allocated ServerChannel.
    pub fn allocate_server_channel(
        &mut self,
        proxy: ConnectionReceiverProxy,
    ) -> Option<ServerChannel> {
        let new_channel = ServerChannel::all().find(|sc| !self.server_channels.contains_key(&sc));
        new_channel.map(|channel| {
            info!("Allocating {:?}", channel);
            self.server_channels.insert(channel, proxy);
            channel
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_bluetooth_bredr::ConnectionReceiverMarker;
    use fuchsia_async as fasync;

    fn setup_rfcomm_manager() -> (fasync::Executor, RfcommServer) {
        let exec = fasync::Executor::new().unwrap();
        let rfcomm = RfcommServer::new();

        (exec, rfcomm)
    }

    #[test]
    fn test_allocate_server_channel() {
        let (_exec, mut rfcomm) = setup_rfcomm_manager();

        let expected_free_channels = ServerChannel::all().count();
        assert_eq!(rfcomm.available_server_channels(), expected_free_channels);

        // Allocating a server channel should be OK.
        let (c, _s) = create_proxy::<ConnectionReceiverMarker>().unwrap();
        let first_channel = rfcomm.allocate_server_channel(c.clone()).expect("should allocate");

        // Allocate the remaining n-1 channels.
        while rfcomm.available_server_channels() > 0 {
            assert!(rfcomm.allocate_server_channel(c.clone()).is_some());
        }

        // Allocating another should fail.
        assert_eq!(rfcomm.available_server_channels(), 0);
        assert!(rfcomm.allocate_server_channel(c.clone()).is_none());

        // De-allocating should work.
        rfcomm.free_server_channel(&first_channel);

        // We should be able to allocate another now that space has freed.
        let (c, _s) = create_proxy::<ConnectionReceiverMarker>().unwrap();
        assert!(rfcomm.allocate_server_channel(c).is_some());
    }
}
