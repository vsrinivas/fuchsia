// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Channel, PeerId},
    futures::{
        task::{noop_waker_ref, Context},
        FutureExt,
    },
    log::{info, warn},
    std::collections::{HashMap, HashSet},
};

use crate::rfcomm::{session::Session, ServerChannel};

/// The RfcommServer handles connection requests from local clients and remote
/// peers.
/// It is responsible for allocating local server channels, managing RFCOMM Sessions
/// between us and remote peers, and relaying RFCOMM connections to local clients.
pub struct RfcommServer {
    /// The local RFCOMM Server Channels that have been assigned to clients.
    /// A client is represented as a ConnectionReceiverProxy.
    server_channels: HashMap<ServerChannel, bredr::ConnectionReceiverProxy>,

    /// Sessions between us and a remote device. Each Session will multiplex
    /// RFCOMM connections over a single L2CAP channel. A Session is represented
    /// by a processing task that handles requests over the L2CAP channel.
    /// There can only be one session per remote peer. See RFCOMM Section 5.2.
    sessions: HashMap<PeerId, fasync::Task<()>>,
}

impl RfcommServer {
    pub fn new() -> Self {
        Self { server_channels: HashMap::new(), sessions: HashMap::new() }
    }

    /// Returns true if a session identified by `id` exists and is currently
    /// active.
    /// An RFCOMM Session is active if there is a currently running processing task.
    fn is_active_session(&mut self, id: &PeerId) -> bool {
        if let Some(task) = self.sessions.get_mut(id) {
            // The usage of `noop_waker_ref` is contingent on the `task` not being polled
            // elsewhere.
            // Each RFCOMM Session is stored as a spawned fasync::Task which runs independently.
            // The `task` itself is never polled directly anywhere else as there is no need to
            // drive it to completion. Thus, `is_active_session` is the only location in which
            // the `task` is polled to determine if the RFCOMM Session processing task is ready
            // or not.
            let mut ctx = Context::from_waker(noop_waker_ref());
            return task.poll_unpin(&mut ctx).is_pending();
        }
        false
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
        proxy: bredr::ConnectionReceiverProxy,
    ) -> Option<ServerChannel> {
        let new_channel = ServerChannel::all().find(|sc| !self.server_channels.contains_key(&sc));
        new_channel.map(|channel| {
            info!("Allocating {:?}", channel);
            self.server_channels.insert(channel, proxy);
            channel
        })
    }

    /// Handles an incoming L2CAP connection from the remote peer.
    /// If there is already an active session established with this peer, returns an Error
    /// as there can only be one active session per peer.
    /// Otherwise, creates a new session over `l2cap` and starts an RFCOMM task.
    pub fn new_l2cap_connection(&mut self, id: PeerId, l2cap: Channel) -> Result<(), Error> {
        if self.is_active_session(&id) {
            return Err(format_err!("RFCOMM Session already exists with peer {:?}", id));
        }
        info!("Received new l2cap connection from peer {:?}", id);

        let session_task = fasync::Task::spawn(async move {
            if let Err(e) = Session::create(l2cap).await {
                warn!("Session ended with {:?} error: {:?}", id, e);
            }
        });
        self.sessions.insert(id, session_task);

        Ok(())
    }
}
#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_bluetooth_bredr::ConnectionReceiverMarker;
    use fuchsia_bluetooth::types::Channel;

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

    #[test]
    fn test_new_l2cap_connection() {
        let (mut exec, mut rfcomm) = setup_rfcomm_manager();

        let id = PeerId(123);
        let (remote, channel) = Channel::create();
        assert!(rfcomm.new_l2cap_connection(id, channel).is_ok());

        // The Session should still be active.
        assert!(rfcomm.is_active_session(&id));

        // Remote peer disconnects - drive the background processing task to detect disconnection.
        drop(remote);
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // The session should be inactive now.
        assert!(!rfcomm.is_active_session(&id));
    }
}
