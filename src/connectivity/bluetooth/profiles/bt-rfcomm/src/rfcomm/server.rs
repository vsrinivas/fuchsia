// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::types::{Channel, PeerId},
    futures::{lock::Mutex, FutureExt},
    log::{info, trace},
    std::{
        collections::{HashMap, HashSet},
        convert::TryFrom,
        sync::Arc,
    },
};

use crate::profile::build_rfcomm_protocol;
use crate::rfcomm::{session::Session, ServerChannel};

/// Manages the current clients of the RFCOMM server. Provides an API for
/// registering, unregistering, and relaying RFCOMM channels to clients.
pub struct Clients {
    /// The currently registered clients. Each registered client is identified
    /// by a unique ServerChannel.
    channel_receivers: Mutex<HashMap<ServerChannel, bredr::ConnectionReceiverProxy>>,
}

impl Clients {
    pub fn new() -> Self {
        Self { channel_receivers: Mutex::new(HashMap::new()) }
    }

    /// Returns the number of available spaces for clients that can be registered.
    async fn available_space(&self) -> usize {
        let server_channels = self.channel_receivers.lock().await;
        ServerChannel::all().filter(|sc| !server_channels.contains_key(&sc)).count()
    }

    /// Removes the client that has registered `server_channel`.
    async fn remove(&self, server_channel: &ServerChannel) {
        self.channel_receivers.lock().await.remove(server_channel);
    }

    /// Clears all the registered clients.
    async fn clear(&self) {
        self.channel_receivers.lock().await.clear();
    }

    /// Reserves the next available ServerChannel for a client represented by a `proxy`.
    ///
    /// If allocated, returns the ServerChannel assigned to the client, None otherwise.
    pub async fn new_client(&self, proxy: bredr::ConnectionReceiverProxy) -> Option<ServerChannel> {
        let mut server_channels = self.channel_receivers.lock().await;
        let new_channel = ServerChannel::all().find(|sc| !server_channels.contains_key(&sc));
        new_channel.map(|channel| {
            trace!("Allocating {:?}", channel);
            server_channels.insert(channel, proxy);
            channel
        })
    }

    /// Delivers the `channel` to the client that has registered the `server_channel`. Returns true
    /// if the channel was delivered to the client.
    pub async fn deliver_channel(
        &self,
        peer_id: PeerId,
        server_channel: ServerChannel,
        channel: Channel,
    ) -> Result<(), Error> {
        if let Some(client) = self.channel_receivers.lock().await.get(&server_channel) {
            // Build the RFCOMM protocol descriptor and relay the channel.
            let mut protocol: Vec<bredr::ProtocolDescriptor> =
                build_rfcomm_protocol(server_channel).iter().map(|p| p.into()).collect();
            return client
                .connected(
                    &mut peer_id.into(),
                    bredr::Channel::try_from(channel).unwrap(),
                    &mut protocol.iter_mut(),
                )
                .map_err(|e| format_err!("{:?}", e));
        }
        Err(format_err!("ServerChannel {:?} not registered", server_channel))
    }
}

/// The RfcommServer handles connection requests from profiles clients and remote peers.
pub struct RfcommServer {
    /// The currently registered profile clients of the RFCOMM server.
    clients: Arc<Clients>,

    /// Sessions between us and a remote device. Each Session will multiplex
    /// RFCOMM connections over a single L2CAP channel.
    /// There can only be one session per remote peer. See RFCOMM Section 5.2.
    sessions: HashMap<PeerId, Session>,
}

impl RfcommServer {
    pub fn new() -> Self {
        Self { clients: Arc::new(Clients::new()), sessions: HashMap::new() }
    }

    /// Returns true if a session identified by `id` exists and is currently
    /// active.
    /// An RFCOMM Session is active if there is a currently running processing task.
    fn is_active_session(&mut self, id: &PeerId) -> bool {
        if let Some(session) = self.sessions.get_mut(id) {
            return session.is_active();
        }
        false
    }

    /// Returns the number of available server channels in this server.
    pub async fn available_server_channels(&self) -> usize {
        self.clients.available_space().await
    }

    /// De-allocates the server `channels` provided.
    pub async fn free_server_channels(&mut self, channels: &HashSet<ServerChannel>) {
        for sc in channels {
            self.clients.remove(sc).await;
        }
    }

    /// De-allocates all the server channels in this server.
    pub async fn free_all_server_channels(&mut self) {
        self.clients.clear().await;
    }

    /// Reserves the next available ServerChannel for a client's `proxy`.
    ///
    /// Returns the allocated ServerChannel.
    pub async fn allocate_server_channel(
        &mut self,
        proxy: bredr::ConnectionReceiverProxy,
    ) -> Option<ServerChannel> {
        self.clients.new_client(proxy).await
    }

    /// Handles an incoming L2CAP connection from the remote peer.
    ///
    /// If there is already an active session established with this peer, returns an Error
    /// as there can only be one active session per peer.
    /// Otherwise, creates and stores a new session over the provided `l2cap` channel.
    pub fn new_l2cap_connection(&mut self, id: PeerId, l2cap: Channel) -> Result<(), Error> {
        if self.is_active_session(&id) {
            return Err(format_err!("RFCOMM Session already exists with peer {:?}", id));
        }
        info!("Received new l2cap connection from peer {:?}", id);

        // Create a new RFCOMM Session with the provided `channel_opened_callback` which will be
        // called anytime an RFCOMM channel is created. Opened RFCOMM channels will be delivered
        // to the `clients` of the `RfcommServer`.
        let clients = self.clients.clone();
        let channel_opened_callback = Box::new(move |server_channel, channel| {
            let peer_id = id;
            let clients = clients.clone();
            async move { clients.deliver_channel(peer_id, server_channel, channel).await }.boxed()
        });
        let session = Session::create(id, l2cap, channel_opened_callback);
        self.sessions.insert(id, session);

        Ok(())
    }
}
#[cfg(test)]
mod tests {
    use super::*;

    use crate::rfcomm::{
        frame::Encodable,
        session::tests::make_sabm_command,
        types::{Role, DLCI},
    };

    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_bredr::ConnectionReceiverMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Channel;
    use futures::{pin_mut, task::Poll, AsyncWriteExt, StreamExt};

    fn setup_rfcomm_manager() -> (fasync::Executor, RfcommServer) {
        let exec = fasync::Executor::new().unwrap();
        let rfcomm = RfcommServer::new();
        (exec, rfcomm)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_server_channel() {
        let mut rfcomm = RfcommServer::new();

        let expected_free_channels = ServerChannel::all().count();
        assert_eq!(rfcomm.available_server_channels().await, expected_free_channels);

        // Allocating a server channel should be OK.
        let (c, _s) = create_proxy::<ConnectionReceiverMarker>().unwrap();
        let first_channel =
            rfcomm.allocate_server_channel(c.clone()).await.expect("should allocate");

        // Allocate the remaining n-1 channels.
        let mut n = expected_free_channels - 1;
        while n > 0 {
            assert!(rfcomm.allocate_server_channel(c.clone()).await.is_some());
            n -= 1;
        }

        // Allocating another should fail.
        assert_eq!(rfcomm.available_server_channels().await, 0);
        assert!(rfcomm.allocate_server_channel(c.clone()).await.is_none());

        // De-allocating should work.
        let mut single_channel = HashSet::new();
        single_channel.insert(first_channel);
        rfcomm.free_server_channels(&single_channel).await;

        // We should be able to allocate another now that space has freed.
        let (c, _s) = create_proxy::<ConnectionReceiverMarker>().unwrap();
        assert!(rfcomm.allocate_server_channel(c).await.is_some());
    }

    #[test]
    fn test_new_l2cap_connection() {
        let (mut exec, mut rfcomm) = setup_rfcomm_manager();

        let id = PeerId(123);
        let (mut remote, channel) = Channel::create();
        assert!(rfcomm.new_l2cap_connection(id, channel).is_ok());

        // The Session should still be active.
        assert!(rfcomm.is_active_session(&id));

        // Simulate peer sending RFCOMM data to the session - should be OK.
        let buf = [0x00, 0x00, 0x00];
        let mut write_fut = remote.write(&buf[..]);
        match exec.run_until_stalled(&mut write_fut) {
            Poll::Ready(Ok(x)) => {
                assert_eq!(x, 3);
            }
            x => panic!("Expected write ready but got {:?}", x),
        }

        // Remote peer disconnects - drive the background processing task to detect disconnection.
        drop(remote);
        let _ = exec.run_until_stalled(&mut futures::future::pending::<()>());

        // The session should be inactive now.
        assert!(!rfcomm.is_active_session(&id));
    }

    #[test]
    fn test_new_rfcomm_channel_is_relayed_to_client() {
        let (mut exec, mut rfcomm) = setup_rfcomm_manager();

        // Profile-client reserves a server channel.
        let (c, mut s) = create_proxy_and_stream::<ConnectionReceiverMarker>().unwrap();
        let first_channel = {
            let fut = rfcomm.allocate_server_channel(c.clone());
            pin_mut!(fut);
            match exec.run_until_stalled(&mut fut) {
                Poll::Ready(Some(sc)) => sc,
                x => panic!("Expected server channel but got {:?}", x),
            }
        };

        let profile_client_fut = s.next();
        pin_mut!(profile_client_fut);
        assert!(exec.run_until_stalled(&mut profile_client_fut).is_pending());

        // Start up a session with remote peer.
        let id = PeerId(1);
        let (remote, channel) = Channel::create();
        assert!(rfcomm.new_l2cap_connection(id, channel).is_ok());
        assert!(rfcomm.is_active_session(&id));

        let mut vec = Vec::new();
        let remote_fut = remote.read_datagram(&mut vec);
        pin_mut!(remote_fut);
        assert!(exec.run_until_stalled(&mut remote_fut).is_pending());

        // Remote device requests to start up session multiplexer.
        let sabm = make_sabm_command(Role::Unassigned, DLCI::MUX_CONTROL_DLCI);
        let mut buf = vec![0; sabm.encoded_len()];
        assert!(sabm.encode(&mut buf[..]).is_ok());
        assert!(remote.as_ref().write(&buf).is_ok());

        // Expect a response to the sent frame.
        assert!(exec.run_until_stalled(&mut remote_fut).is_ready());

        // Remote device requests to open up an RFCOMM channel. The DLCI is the ServerChannel
        // tagged with a direction bit = 0 (since we are responder role).
        let user_dlci = DLCI::try_from(first_channel.0 << 1).unwrap();
        let user_sabm = make_sabm_command(Role::Initiator, user_dlci);
        let mut buf = vec![0; sabm.encoded_len()];
        assert!(user_sabm.encode(&mut buf[..]).is_ok());
        assert!(remote.as_ref().write(&buf).is_ok());

        // Expect a response to the sent frame.
        let mut vec = Vec::new();
        let remote_fut = remote.read_datagram(&mut vec);
        pin_mut!(remote_fut);
        assert!(exec.run_until_stalled(&mut remote_fut).is_ready());

        // The Session should open a new RFCOMM channel for the provided `user_dlci`, and
        // the Channel should be relayed to the profile client.
        match exec.run_until_stalled(&mut profile_client_fut) {
            Poll::Ready(Some(Ok(bredr::ConnectionReceiverRequest::Connected { .. }))) => {}
            x => panic!("Expected connection but got {:?}", x),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register_and_deliver_channel_to_clients() {
        let clients = Clients::new();

        // Initial capacity is the range of all valid Server Channels (1..30).
        let mut expected_space = 30;
        assert_eq!(clients.available_space().await, expected_space);

        // Attempting to deliver a channel for an unregistered ServerChannel should be an error.
        let random_server_channel = ServerChannel(10);
        let (local, _remote) = Channel::create();
        assert!(clients.deliver_channel(PeerId(1), random_server_channel, local).await.is_err());

        // Registering a new client should be OK.
        let (c, s) = create_proxy_and_stream::<bredr::ConnectionReceiverMarker>().unwrap();
        let server_channel = clients.new_client(c).await.unwrap();
        expected_space -= 1;
        assert_eq!(clients.available_space().await, expected_space);

        // Delivering channel to registered client should be OK.
        let (local, _remote) = Channel::create();
        assert!(clients.deliver_channel(PeerId(1), server_channel, local).await.is_ok());

        // Client disconnects - delivering a new channel should fail.
        drop(s);
        let (local, _remote) = Channel::create();
        assert!(clients.deliver_channel(PeerId(1), server_channel, local).await.is_err());
    }
}
