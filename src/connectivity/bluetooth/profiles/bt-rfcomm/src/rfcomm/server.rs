// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth::ErrorCode,
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

    /// Delivers the `channel` to the client that has registered the `server_channel`.
    /// Returns an error if delivery fails or if there is no such client.
    pub async fn deliver_channel(
        &self,
        peer_id: PeerId,
        server_channel: ServerChannel,
        channel: Channel,
    ) -> Result<(), Error> {
        let clients = self.channel_receivers.lock().await;
        let client = clients
            .get(&server_channel)
            .ok_or(format_err!("ServerChannel {:?} not registered", server_channel))?;
        // Build the RFCOMM protocol descriptor and relay the channel.
        let mut protocol: Vec<bredr::ProtocolDescriptor> =
            build_rfcomm_protocol(server_channel).iter().map(Into::into).collect();
        client
            .connected(
                &mut peer_id.into(),
                bredr::Channel::try_from(channel).unwrap(),
                &mut protocol.iter_mut(),
            )
            .map_err(|e| format_err!("{:?}", e))
    }
}

/// The RfcommServer handles connection requests from profiles clients and remote peers.
pub struct RfcommServer {
    /// The currently registered profile clients of the RFCOMM server.
    clients: Arc<Clients>,

    /// Sessions between us and a remote peer. Each Session will multiplex
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
    pub fn is_active_session(&mut self, id: &PeerId) -> bool {
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

    /// Opens an RFCOMM channel specified by `server_channel` with the remote peer.
    ///
    /// Returns an error if there is no session established with the peer.
    pub async fn open_rfcomm_channel(
        &mut self,
        id: PeerId,
        server_channel: ServerChannel,
        responder: bredr::ProfileConnectResponder,
    ) -> Result<(), Error> {
        trace!("Received request to open RFCOMM channel {:?} with peer {:?}", server_channel, id);
        match self.sessions.get_mut(&id) {
            None => {
                let _ = responder.send(&mut Err(ErrorCode::Failed));
                Err(format_err!("Invalid peer ID {:?}", id))
            }
            Some(session) => {
                let channel_opened_callback =
                    Box::new(move |channel: Result<Channel, ErrorCode>| {
                        let mut channel = channel.map(|c| bredr::Channel::try_from(c).unwrap());
                        responder.send(&mut channel).map_err(|e| format_err!("{:?}", e))
                    });
                session.open_rfcomm_channel(server_channel, channel_opened_callback).await;
                Ok(())
            }
        }
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
pub(crate) mod tests {
    use super::*;

    use crate::rfcomm::{
        frame::mux_commands::*,
        frame::*,
        types::{CommandResponse, Role, DLCI},
    };

    use fidl::{
        encoding::Decodable,
        endpoints::{create_proxy, create_proxy_and_stream},
    };
    use fidl_fuchsia_bluetooth_bredr::ConnectionReceiverMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Channel;
    use futures::{pin_mut, task::Poll, AsyncWriteExt, StreamExt};
    use matches::assert_matches;

    fn setup_rfcomm_manager() -> (fasync::Executor, RfcommServer) {
        let exec = fasync::Executor::new().unwrap();
        let rfcomm = RfcommServer::new();
        (exec, rfcomm)
    }

    #[track_caller]
    pub fn send_peer_frame(remote: &fidl::Socket, frame: Frame) {
        let mut buf = vec![0; frame.encoded_len()];
        assert!(frame.encode(&mut buf[..]).is_ok());
        assert!(remote.write(&buf).is_ok());
    }

    #[track_caller]
    pub fn expect_frame_received_by_peer(exec: &mut fasync::Executor, remote: &mut Channel) {
        let mut vec = Vec::new();
        let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
        assert!(exec.run_until_stalled(&mut remote_fut).is_ready());
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
        // Checking again is OK.
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
        let (local, mut remote) = Channel::create();
        assert!(rfcomm.new_l2cap_connection(id, local).is_ok());
        assert!(rfcomm.is_active_session(&id));

        // Remote peer requests to start up session multiplexer.
        let sabm = Frame::make_sabm_command(Role::Unassigned, DLCI::MUX_CONTROL_DLCI);
        send_peer_frame(remote.as_ref(), sabm);
        // Expect to send a positive response to the peer.
        expect_frame_received_by_peer(&mut exec, &mut remote);

        // Remote peer requests to open an RFCOMM channel.
        let user_dlci = first_channel.to_dlci(Role::Responder).unwrap();
        let user_sabm = Frame::make_sabm_command(Role::Initiator, user_dlci);
        send_peer_frame(remote.as_ref(), user_sabm);
        // Expect to send a positive response to the peer.
        expect_frame_received_by_peer(&mut exec, &mut remote);

        // The Session should open a new RFCOMM channel for the provided `user_dlci`, and
        // the Channel should be relayed to the profile client.
        match exec.run_until_stalled(&mut profile_client_fut) {
            Poll::Ready(Some(Ok(bredr::ConnectionReceiverRequest::Connected { .. }))) => {}
            x => panic!("Expected connection but got {:?}", x),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_register_and_deliver_inbound_channel_to_clients() {
        let clients = Clients::new();

        // Initial capacity is the range of all valid Server Channels (1..30).
        let mut expected_space = 30;
        assert_eq!(clients.available_space().await, expected_space);

        // Attempting to deliver an inbound channel for an unregistered ServerChannel should be
        // an error.
        let random_server_channel = ServerChannel::try_from(10).unwrap();
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

    /// Makes a client Profile::Connect() request and returns the responder for the request
    /// and a Future associated with the request.
    #[track_caller]
    fn make_client_connect_request(
        exec: &mut fasync::Executor,
        id: PeerId,
    ) -> (
        bredr::ProfileConnectResponder,
        fidl::client::QueryResponseFut<Result<bredr::Channel, ErrorCode>>,
    ) {
        let (profile, mut profile_server) =
            create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        let mut profile_stream = Box::pin(profile_server.next());
        let connect_request =
            profile.connect(&mut id.into(), &mut bredr::ConnectParameters::new_empty());
        let responder = match exec.run_until_stalled(&mut profile_stream) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Connect { responder, .. }))) => responder,
            x => panic!("Expected ready connect request but got: {:?}", x),
        };
        (responder, connect_request)
    }

    #[test]
    fn test_request_outbound_connection_succeeds() {
        let (mut exec, mut rfcomm) = setup_rfcomm_manager();

        // Start up a session with remote peer.
        let id = PeerId(1);
        let (local, mut remote) = Channel::create();
        assert!(rfcomm.new_l2cap_connection(id, local).is_ok());

        // Simulate a client connect request.
        let (responder, connect_request_fut) = make_client_connect_request(&mut exec, id);
        pin_mut!(connect_request_fut);
        assert!(exec.run_until_stalled(&mut connect_request_fut).is_pending());
        // We expect the open channel request to be OK - still awaiting the channel.
        let server_channel = ServerChannel::try_from(9).unwrap();
        let expected_dlci = server_channel.to_dlci(Role::Responder).unwrap();
        let mut outbound_fut = Box::pin(rfcomm.open_rfcomm_channel(id, server_channel, responder));
        assert_matches!(exec.run_until_stalled(&mut outbound_fut), Poll::Ready(Ok(_)));
        assert!(exec.run_until_stalled(&mut connect_request_fut).is_pending());

        // Expect to send a frame to the peer - SABM for mux startup.
        expect_frame_received_by_peer(&mut exec, &mut remote);
        // Simulate peer responding positively.
        let ua = Frame::make_ua_response(Role::Unassigned, DLCI::MUX_CONTROL_DLCI);
        send_peer_frame(remote.as_ref(), ua);

        // Expect to send a frame to peer - parameter negotiation.
        expect_frame_received_by_peer(&mut exec, &mut remote);
        // Simulate peer responding positively.
        let data = MuxCommand {
            params: MuxCommandParams::ParameterNegotiation(ParameterNegotiationParams {
                dlci: expected_dlci,
                credit_based_flow_handshake: CreditBasedFlowHandshake::SupportedResponse,
                priority: 12,
                max_frame_size: 100,
                initial_credits: 1,
            }),
            command_response: CommandResponse::Response,
        };
        let pn_response = Frame::make_mux_command(Role::Responder, data);
        send_peer_frame(remote.as_ref(), pn_response);

        // Expect to send a frame to peer - SABM for channel opening.
        expect_frame_received_by_peer(&mut exec, &mut remote);
        // Simulate peer responding positively.
        let ua = Frame::make_ua_response(Role::Responder, expected_dlci);
        send_peer_frame(remote.as_ref(), ua);

        // The channel should be established and relayed to the client that requested it.
        assert_matches!(exec.run_until_stalled(&mut connect_request_fut), Poll::Ready(Ok(Ok(_))));
    }

    #[test]
    fn test_request_outbound_connection_invalid_peer() {
        let (mut exec, mut rfcomm) = setup_rfcomm_manager();

        // Simulate a client connect request.
        let random_id = PeerId(41);
        let (responder, connect_request_fut) = make_client_connect_request(&mut exec, random_id);
        pin_mut!(connect_request_fut);
        assert!(exec.run_until_stalled(&mut connect_request_fut).is_pending());

        // We expect the open channel request to fail.
        let server_channel = ServerChannel::try_from(8).unwrap();
        let mut outbound_fut =
            Box::pin(rfcomm.open_rfcomm_channel(random_id, server_channel, responder));
        assert_matches!(exec.run_until_stalled(&mut outbound_fut), Poll::Ready(Err(_)));
        // Responder should be notified of failure.
        assert_matches!(exec.run_until_stalled(&mut connect_request_fut), Poll::Ready(Ok(Err(_))));
    }
}
