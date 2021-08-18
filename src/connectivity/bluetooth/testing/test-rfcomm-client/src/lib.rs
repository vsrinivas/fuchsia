// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use bt_rfcomm::{profile::server_channel_from_protocol, ServerChannel};
use derivative::Derivative;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_bluetooth_rfcomm_test as rfcomm;
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::{Channel, PeerId, Uuid};
use futures::{channel::mpsc, select, FutureExt, StreamExt};
use parking_lot::Mutex;
use profile_client::{ProfileClient, ProfileEvent};
use std::{collections::HashMap, convert::TryFrom, sync::Arc};
use tracing::{info, warn};

/// The default buffer size for the mpsc channels used to relay user data packets to be sent to the
/// remote peer.
/// This value is arbitrarily chosen and should be enough to queue multiple buffers to be sent.
const USER_DATA_BUFFER_SIZE: usize = 50;

/// Valid SPP Service Definition - see SPP v1.2 Table 6.1.
fn spp_service_definition() -> bredr::ServiceDefinition {
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(
            bredr::ServiceClassProfileIdentifier::SerialPort as u16,
        )
        .into()]),
        protocol_descriptor_list: Some(vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![],
            },
        ]),
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::SerialPort,
            major_version: 1,
            minor_version: 2,
        }]),
        ..bredr::ServiceDefinition::EMPTY
    }
}

/// Manages the set of active RFCOMM channels connected to a single remote peer.
#[derive(Debug)]
pub struct RfcommSession {
    /// Unique id assigned to the remote peer.
    _id: PeerId,
    /// The set of active RFCOMM channels.
    active_channels: HashMap<ServerChannel, mpsc::Sender<Vec<u8>>>,
}

impl RfcommSession {
    fn new(id: PeerId) -> Self {
        Self { _id: id, active_channels: HashMap::new() }
    }

    fn is_active(&self, server_channel: &ServerChannel) -> bool {
        self.active_channels.get(server_channel).map_or(false, |s| !s.is_closed())
    }

    fn close_rfcomm_channel(&mut self, server_channel: &ServerChannel) -> bool {
        self.active_channels.remove(server_channel).is_some()
    }

    fn new_rfcomm_channel(&mut self, server_channel: ServerChannel, channel: Channel) {
        if self.is_active(&server_channel) {
            info!("Overwriting existing RFCOMM channel: {:?}", server_channel);
        }

        let (sender, receiver) = mpsc::channel(USER_DATA_BUFFER_SIZE);
        fasync::Task::spawn(Self::rfcomm_channel_task(server_channel, channel, receiver)).detach();
        let _ = self.active_channels.insert(server_channel, sender);
    }

    /// Processes data received from the remote peer over the provided RFCOMM `channel`.
    /// Processes data in the `write_requests` queue to be sent to the remote peer.
    async fn rfcomm_channel_task(
        server_channel: ServerChannel,
        mut channel: Channel,
        mut write_requests: mpsc::Receiver<Vec<u8>>,
    ) {
        info!("Starting processing task for RFCOMM channel: {:?}", server_channel);
        loop {
            select! {
                // The `fuse()` call is in the loop because `channel` is both borrowed as a stream
                // and used to send data. It is safe because once `channel` is closed, the loop will
                // break and `channel.next()` will never be polled thereafter.
                bytes_from_peer = channel.next().fuse() => {
                    let user_data = match bytes_from_peer {
                        Some(Ok(bytes)) => bytes,
                        Some(Err(e)) => {
                            info!("Error receiving data: {:?}", e);
                            continue;
                        }
                        None => {
                            // RFCOMM channel closed by the peer.
                            info!("Peer closed RFCOMM channel {:?}", server_channel);
                            break;
                        }
                    };
                    info!("{:?}: Received user data from peer: {:?}", server_channel, user_data);
                }
                bytes_to_peer = write_requests.next() => {
                    match bytes_to_peer {
                        Some(bytes) => {
                            match channel.as_ref().write(&bytes) {
                                Ok(_) => info!("Sent user data over RFCOMM channel ({:?}).", server_channel),
                                Err(e) => info!("Couldn't send user data for channel ({:?}): {:?}", server_channel, e),
                            }
                        }
                        None => break, // RFCOMM channel closed by us.
                    }
                }
                complete => break,
            }
        }
        info!("RFCOMM channel ({:?}) task ended", server_channel);
    }

    /// Sends the `user_data` buf to the peer that provides the service identified by the
    /// `server_channel`. Returns the result of the send operation.
    fn send_user_data(
        &mut self,
        server_channel: ServerChannel,
        user_data: Vec<u8>,
    ) -> Result<(), Error> {
        if let Some(sender) = self.active_channels.get_mut(&server_channel) {
            sender.try_send(user_data).map_err(|e| format_err!("{:?}", e))
        } else {
            Err(format_err!("No registered server channel"))
        }
    }
}

#[derive(Derivative)]
#[derivative(Debug)]
pub struct RfcommState {
    /// A task representing the RFCOMM service advertisement and search.
    #[derivative(Debug = "ignore")]
    service: Option<fasync::Task<()>>,
    /// The set of active RFCOMM Sessions with remote peers.
    active_sessions: HashMap<PeerId, RfcommSession>,
}

impl RfcommState {
    fn new() -> Self {
        Self { service: None, active_sessions: HashMap::new() }
    }

    fn get_active_session(&mut self, id: &PeerId) -> Option<&mut RfcommSession> {
        match self.active_sessions.get_mut(id) {
            None => {
                info!("No active RFCOMM session with peer {}", id);
                None
            }
            session => session,
        }
    }

    fn clear_services(&mut self) {
        if let Some(old_task) = self.service.take() {
            info!("Clearing SPP service advertisement/search");
            let _ = old_task.cancel();
        }
        self.active_sessions.clear();
    }

    fn new_rfcomm_channel(&mut self, id: PeerId, server_channel: ServerChannel, channel: Channel) {
        let _ = self
            .active_sessions
            .entry(id)
            .or_insert(RfcommSession::new(id))
            .new_rfcomm_channel(server_channel, channel);
    }
}

#[derive(Clone, Debug)]
pub struct RfcommManager {
    profile: bredr::ProfileProxy,
    rfcomm: rfcomm::RfcommTestProxy,
    inner: Arc<Mutex<RfcommState>>,
}

impl RfcommManager {
    pub fn new() -> Result<Self, Error> {
        let profile = fuchsia_component::client::connect_to_protocol::<bredr::ProfileMarker>()?;
        let rfcomm_test =
            fuchsia_component::client::connect_to_protocol::<rfcomm::RfcommTestMarker>()?;
        Ok(Self::from_proxy(profile, rfcomm_test))
    }

    pub fn from_proxy(profile: bredr::ProfileProxy, rfcomm: rfcomm::RfcommTestProxy) -> Self {
        Self { profile, rfcomm, inner: Arc::new(Mutex::new(RfcommState::new())) }
    }

    pub fn clear_services(&self) {
        self.inner.lock().clear_services();
    }

    /// Advertises an SPP service and searches for other compatible SPP clients. Overwrites any
    /// existing service advertisement & search.
    pub fn advertise(&self) -> Result<(), Error> {
        // Existing service must be unregistered before we can advertise again - this is to prevent
        // clashes in `bredr.Profile` server.
        self.clear_services();

        let inner_clone = self.inner.clone();
        let mut inner = self.inner.lock();

        // Add an SPP advertisement & search.
        let spp_service = vec![spp_service_definition()];
        let mut client = ProfileClient::advertise(
            self.profile.clone(),
            &spp_service,
            bredr::ChannelParameters::EMPTY,
        )?;
        let _ = client.add_search(bredr::ServiceClassProfileIdentifier::SerialPort, &[])?;
        let service_task = fasync::Task::spawn(async move {
            let result = Self::handle_profile_events(client, inner_clone).await;
            info!("Profile event handler ended: {:?}", result);
        });
        inner.service = Some(service_task);
        info!("Advertising and searching for SPP services");
        Ok(())
    }

    /// Processes events from the `bredr.Profile` `client`.
    async fn handle_profile_events(
        mut client: ProfileClient,
        state: Arc<Mutex<RfcommState>>,
    ) -> Result<(), Error> {
        while let Some(request) = client.next().await {
            match request {
                Ok(ProfileEvent::PeerConnected { id, protocol, channel, .. }) => {
                    // Received an incoming connection request for our advertised service.
                    let protocol = protocol.iter().map(Into::into).collect();
                    let server_channel = server_channel_from_protocol(&protocol)
                        .ok_or(format_err!("Not RFCOMM protocol"))?;

                    // Spawn a processing task to handle read & writes over this RFCOMM channel.
                    state.lock().new_rfcomm_channel(id, server_channel, channel);
                    info!("Peer {} established RFCOMM Channel ({:?}) ", id, server_channel);
                }
                Ok(ProfileEvent::SearchResult { id, protocol, .. }) => {
                    // Discovered a remote peer's service.
                    let protocol =
                        protocol.expect("Protocol should exist").iter().map(Into::into).collect();
                    let server_channel = server_channel_from_protocol(&protocol)
                        .ok_or(format_err!("Not RFCOMM protocol"))?;
                    info!("Found SPP service for {} with server channel: {:?}", id, server_channel);
                }
                Err(e) => warn!("Error in ProfileClient results: {:?}", e),
            }
        }
        Ok(())
    }

    /// Terminates the RFCOMM session with the remote peer `id`.
    pub fn close_session(&self, id: PeerId) -> Result<(), Error> {
        // Send the disconnect request via the `RfcommTest` API and clean up local state.
        let _ = self.rfcomm.disconnect(&mut id.into()).map_err::<fidl::Error, _>(Into::into)?;

        let mut inner = self.inner.lock();
        if let Some(session) = inner.active_sessions.remove(&id) {
            drop(session);
        }
        Ok(())
    }

    /// Closes the RFCOMM channel with the remote peer.
    pub fn close_rfcomm_channel(
        &self,
        id: PeerId,
        server_channel: ServerChannel,
    ) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        if let Some(session) = inner.get_active_session(&id) {
            let _ = session.close_rfcomm_channel(&server_channel);
            Ok(())
        } else {
            Err(format_err!("No RFCOMM session with peer: {:?}", id))
        }
    }

    /// Makes an outgoing RFCOMM channel to the remote peer.
    pub async fn outgoing_rfcomm_channel(
        &self,
        id: PeerId,
        server_channel: ServerChannel,
    ) -> Result<(), Error> {
        let channel = self
            .profile
            .connect(
                &mut id.into(),
                &mut bredr::ConnectParameters::Rfcomm(bredr::RfcommParameters {
                    channel: Some(server_channel.into()),
                    ..bredr::RfcommParameters::EMPTY
                }),
            )
            .await?
            .map_err(|e| format_err!("{:?}", e))?;
        let channel = Channel::try_from(channel).expect("valid channel");

        self.inner.lock().new_rfcomm_channel(id, server_channel, channel);
        Ok(())
    }

    /// Send a Remote Line Status update for the RFCOMM `server_channel` with peer `id`. Returns
    /// Error if there is no such established RFCOMM channel with the peer.
    pub fn send_rls(&self, id: PeerId, server_channel: ServerChannel) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        if inner.get_active_session(&id).is_some() {
            // Send a fixed Framing error status.
            let status = rfcomm::Status::FramingError;
            let _ = self
                .rfcomm
                .remote_line_status(&mut id.into(), server_channel.into(), status)
                .map_err::<fidl::Error, _>(Into::into)?;
            Ok(())
        } else {
            Err(format_err!("No RFCOMM session with peer: {:?}", id))
        }
    }

    /// Attempts to send user `data` to the remote peer `id`. Returns Error if there is no such
    /// established RFCOMM channel with the peer.
    pub fn send_user_data(
        &self,
        id: PeerId,
        server_channel: ServerChannel,
        data: Vec<u8>,
    ) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        if let Some(session) = inner.get_active_session(&id) {
            session.send_user_data(server_channel, data)
        } else {
            Err(format_err!("No RFCOMM session with peer: {:?}", id))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_utils::PollExt;
    use bt_rfcomm::profile::build_rfcomm_protocol;
    use fidl::endpoints::Proxy;
    use fidl_fuchsia_bluetooth::ErrorCode;
    use fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileRequestStream};
    use fidl_fuchsia_bluetooth_rfcomm_test::{RfcommTestMarker, RfcommTestRequestStream};
    use fixture::fixture;
    use matches::assert_matches;
    use std::convert::TryInto;

    type TestFixture = (RfcommManager, ProfileRequestStream, RfcommTestRequestStream);

    async fn setup_rfcomm_mgr<F, Fut>(_name: &str, test: F)
    where
        F: FnOnce(TestFixture) -> Fut,
        Fut: futures::Future<Output = ()>,
    {
        let (profile, profile_server) =
            fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
        let (rfcomm_test, rfcomm_test_server) =
            fidl::endpoints::create_proxy_and_stream::<RfcommTestMarker>().unwrap();

        let rfcomm_mgr = RfcommManager::from_proxy(profile, rfcomm_test);
        test((rfcomm_mgr, profile_server, rfcomm_test_server)).await
    }

    #[track_caller]
    async fn expect_data(remote: &mut Channel, expected_data: Vec<u8>) {
        let mut vec = Vec::new();
        let read_result = remote.read_datagram(&mut vec).await;
        assert_eq!(read_result, Ok(expected_data.len()));
        assert_eq!(vec, expected_data);
    }

    #[track_caller]
    async fn expect_advertisement_and_search(
        profile: &mut ProfileRequestStream,
    ) -> (
        bredr::SearchResultsProxy,
        (bredr::ConnectionReceiverProxy, bredr::ProfileAdvertiseResponder),
    ) {
        let mut search_request = None;
        let mut advertisement = None;
        while let Some(req) = profile.next().await {
            match req {
                Ok(bredr::ProfileRequest::Advertise { receiver, responder, .. }) => {
                    let connect_proxy = receiver.into_proxy().unwrap();
                    advertisement = Some((connect_proxy, responder));
                }
                Ok(bredr::ProfileRequest::Search { results, .. }) => {
                    search_request = Some(results.into_proxy().unwrap())
                }
                x => panic!("Expected one Advertise and Search but got: {:?}", x),
            }
            if search_request.is_some() && advertisement.is_some() {
                break;
            }
        }
        (search_request.expect("just set"), advertisement.expect("just set"))
    }

    #[fixture(setup_rfcomm_mgr)]
    #[fuchsia::test]
    async fn initiate_rfcomm_channel_to_peer_is_ok(
        (rfcomm_mgr, mut profile_server, mut rfcomm_test_server): TestFixture,
    ) {
        // Keep the `bredr.Profile` requests alive - one advertisement and search.
        let _profile_requests = {
            assert_matches!(rfcomm_mgr.advertise(), Ok(_));
            expect_advertisement_and_search(&mut profile_server).await
        };

        // Can establish RFCOMM channel to peer.
        let remote_id = PeerId(123);
        let random_channel_number = ServerChannel::try_from(5).unwrap();
        let mut peer_channel = {
            let ch_fut =
                Box::pin(rfcomm_mgr.outgoing_rfcomm_channel(remote_id, random_channel_number));

            let profile_fut = async {
                match profile_server.next().await {
                    Some(Ok(bredr::ProfileRequest::Connect { responder, .. })) => {
                        let (left, right) = Channel::create();
                        let _ = responder
                            .send(&mut left.try_into().map_err(|_e| ErrorCode::Failed))
                            .unwrap();
                        right
                    }
                    x => panic!("Expected connect request, got: {:?}", x),
                }
            };

            match futures::future::join(ch_fut, profile_fut).await {
                (Ok(_), channel) => channel,
                x => panic!("Expected both futures to complete: {:?}", x),
            }
        };

        // Sending data to the peer is ok.
        let user_data = vec![0x98, 0x97, 0x96, 0x95];
        {
            assert_matches!(
                rfcomm_mgr.send_user_data(remote_id, random_channel_number, user_data.clone()),
                Ok(_)
            );
            expect_data(&mut peer_channel, user_data).await;
        }

        // Peer sends us data. It should be received gracefully and logged (nothing to test).
        let buf = vec![0x99, 0x11, 0x44];
        assert_eq!(peer_channel.as_ref().write(&buf), Ok(3));

        // Test client can request to send an RLS update - should be received by RFCOMM Test server.
        assert_matches!(rfcomm_mgr.send_rls(remote_id, random_channel_number), Ok(_));
        match rfcomm_test_server.next().await.expect("valid fidl request") {
            Ok(rfcomm::RfcommTestRequest::RemoteLineStatus { id, channel_number, .. }) => {
                assert_eq!(id, remote_id.into());
                assert_eq!(channel_number, random_channel_number.into());
            }
            x => panic!("Expected RLS request but got: {:?}", x),
        }
    }

    #[fixture(setup_rfcomm_mgr)]
    #[fuchsia::test]
    async fn peer_initiating_rfcomm_channel_is_delivered(
        (rfcomm_mgr, mut profile_server, _rfcomm_test_server): TestFixture,
    ) {
        // Keep the `bredr.Profile` requests alive - one advertisement and search.
        let (_search_proxy, (connect_proxy, _adv_fut)) = {
            assert_matches!(rfcomm_mgr.advertise(), Ok(_));
            expect_advertisement_and_search(&mut profile_server).await
        };

        // Peer connects to us.
        let remote_id = PeerId(8978);
        let random_channel_number = ServerChannel::try_from(7).unwrap();
        let (_peer_channel, local_channel) = Channel::create();
        let mut protocol: Vec<bredr::ProtocolDescriptor> =
            build_rfcomm_protocol(random_channel_number).iter().map(Into::into).collect();
        assert_matches!(
            connect_proxy.connected(
                &mut remote_id.into(),
                local_channel.try_into().unwrap(),
                &mut protocol.iter_mut()
            ),
            Ok(_)
        );
    }

    #[fixture(setup_rfcomm_mgr)]
    #[fuchsia::test]
    async fn disconnect_session_received_by_rfcomm_test(
        (rfcomm_mgr, mut profile_server, mut rfcomm_test_server): TestFixture,
    ) {
        // Keep the `bredr.Profile` requests alive - one advertisement and search.
        let _profile_requests = {
            assert_matches!(rfcomm_mgr.advertise(), Ok(_));
            expect_advertisement_and_search(&mut profile_server).await
        };

        // Even though there are no active RFCOMM channels established, a client can still request
        // to disconnect the session - expect it to be received.
        let remote = PeerId(834);
        assert_matches!(rfcomm_mgr.close_session(remote), Ok(_));

        match rfcomm_test_server.next().await.expect("valid fidl request") {
            Ok(rfcomm::RfcommTestRequest::Disconnect { id, .. }) if id == remote.into() => {}
            x => panic!("Expected Disconnect request but got: {:?}", x),
        }
    }

    #[fixture(setup_rfcomm_mgr)]
    #[fuchsia::test]
    async fn rls_update_before_established_channel_is_error(
        (rfcomm_mgr, mut profile_server, _rfcomm_test_server): TestFixture,
    ) {
        // Keep the `bredr.Profile` requests alive - one advertisement and search.
        let _profile_requests = {
            assert_matches!(rfcomm_mgr.advertise(), Ok(_));
            expect_advertisement_and_search(&mut profile_server).await
        };

        // RLS updates pertain to a specific RFCOMM channel. Expect an error if an RLS request is
        // sent for a non existent channel.
        let remote = PeerId(222);
        let random_channel_number = ServerChannel::try_from(9).unwrap();
        assert_matches!(rfcomm_mgr.send_rls(remote, random_channel_number), Err(_));
    }

    #[fixture(setup_rfcomm_mgr)]
    #[fuchsia::test]
    async fn clear_services_unregisters_profile_requests(
        (rfcomm_mgr, mut profile_server, _rfcomm_test_server): TestFixture,
    ) {
        // Keep the `bredr.Profile` requests alive - one advertisement and search.
        let (search_proxy, (connect_proxy, _advertise_fut)) = {
            assert_matches!(rfcomm_mgr.advertise(), Ok(_));
            expect_advertisement_and_search(&mut profile_server).await
        };
        assert!(!search_proxy.is_closed());
        assert!(!connect_proxy.is_closed());

        // Clearing services should unregister advertisement and search (transitively closing the
        // FIDL channels).
        // Note: Clearing `Profile` services cancels the fasync::Task processing the `bredr.Profile`
        // requests. Per documentation of fasync::Task, there are no guarantees about the freeing
        // of resources held by a Task. Therefore, we cannot assume `search_proxy` and
        // `connect_proxy` will be closed immediately (but we do expect them to be freed eventually)
        rfcomm_mgr.clear_services();

        // Can register again.
        let _profile = {
            assert_matches!(rfcomm_mgr.advertise(), Ok(_));
            expect_advertisement_and_search(&mut profile_server).await
        };
    }

    #[fuchsia::test]
    async fn rfcomm_session_task() {
        let id = PeerId(999);
        let mut session = RfcommSession::new(id);

        let random_channel_number = ServerChannel::try_from(4).unwrap();
        let (local, mut remote) = Channel::create();
        session.new_rfcomm_channel(random_channel_number, local);

        assert!(session.is_active(&random_channel_number));

        let data = vec![0x00, 0x02, 0x04, 0x06, 0x08, 0x10];
        let unregistered = ServerChannel::try_from(9).unwrap();
        // Unregistered channel number is error.
        assert_matches!(session.send_user_data(unregistered, data.clone()), Err(_));
        // Sending is OK.
        assert_matches!(session.send_user_data(random_channel_number, data.clone()), Ok(_));

        // Should be received by remote.
        expect_data(&mut remote, data).await;

        // Can send multiple buffers.
        let data1 = vec![0x09];
        let data2 = vec![0x11];
        assert_matches!(session.send_user_data(random_channel_number, data1.clone()), Ok(_));
        assert_matches!(session.send_user_data(random_channel_number, data2.clone()), Ok(_));
        expect_data(&mut remote, data1).await;
        expect_data(&mut remote, data2).await;

        // Local wants to close channel - should disconnect.
        assert!(session.close_rfcomm_channel(&random_channel_number));
        assert_matches!(remote.closed().await, Ok(_));

        // Trying again is OK - nothing happens.
        assert!(!session.close_rfcomm_channel(&random_channel_number));
    }

    #[fuchsia::test]
    async fn second_channel_overwrites_first_in_rfcomm_session() {
        let id = PeerId(78);
        let mut session = RfcommSession::new(id);

        let random_channel_number = ServerChannel::try_from(10).unwrap();
        let (local1, remote1) = Channel::create();
        session.new_rfcomm_channel(random_channel_number, local1);
        assert!(session.is_active(&random_channel_number));

        // Can create a new RFCOMM channel, this will overwrite the existing one.
        let (local2, mut remote2) = Channel::create();
        session.new_rfcomm_channel(random_channel_number, local2);
        assert!(session.is_active(&random_channel_number));

        assert_matches!(remote1.closed().await, Ok(_));

        let data = vec![0x00, 0x02, 0x04, 0x06, 0x08, 0x10];
        // Sending is OK - should be received by remote.
        assert_matches!(session.send_user_data(random_channel_number, data.clone()), Ok(_));
        expect_data(&mut remote2, data).await;
    }

    #[fuchsia::test]
    fn closing_sender_closes_rfcomm_channel_task() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let random_channel_number = ServerChannel::try_from(10).unwrap();
        let (local, _remote) = Channel::create();
        let (_sender, receiver) = mpsc::channel(0);

        let mut channel_task =
            Box::pin(RfcommSession::rfcomm_channel_task(random_channel_number, local, receiver));

        exec.run_until_stalled(&mut channel_task).expect_pending("sender still active");

        drop(_sender);
        let _ = exec.run_until_stalled(&mut channel_task).expect("task should complete");
    }

    #[fuchsia::test]
    fn closing_channel_closes_rfcomm_channel_task() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let random_channel_number = ServerChannel::try_from(10).unwrap();
        let (local, _remote) = Channel::create();
        let (_sender, receiver) = mpsc::channel(0);

        let mut channel_task =
            Box::pin(RfcommSession::rfcomm_channel_task(random_channel_number, local, receiver));

        exec.run_until_stalled(&mut channel_task).expect_pending("sender still active");

        drop(_remote);
        let _ = exec.run_until_stalled(&mut channel_task).expect("task should complete");
    }
}
