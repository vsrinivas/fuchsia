// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    async_utils::stream::{StreamItem, WithEpitaph, WithTag},
    fidl::endpoints::ClientEnd,
    fidl::prelude::*,
    fidl_fuchsia_bluetooth::ErrorCode,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_sys::EnvironmentOptions,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{profile::Psm, types::PeerId},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{self, channel::mpsc, future::FutureExt, select, sink::SinkExt, stream::StreamExt},
    parking_lot::Mutex,
    std::{
        collections::{hash_map::Entry, HashMap, HashSet},
        convert::TryFrom,
        sync::Arc,
    },
    tracing::{error, info},
};

mod peer;
mod profile;
mod types;

use crate::{peer::MockPeer, types::LaunchInfo};

/// The MockPiconetServer implements both the bredr.Profile service and the bredr.ProfileTest
/// service. The server is responsible for routing incoming asynchronous requests from peers in
/// the piconet.
pub struct MockPiconetServer {
    inner: Arc<Mutex<MockPiconetServerInner>>,
    proxy_launcher_enabled: bool,
}

impl MockPiconetServer {
    pub fn new(proxy_launcher_enabled: bool) -> Self {
        Self { inner: Arc::new(Mutex::new(MockPiconetServerInner::new())), proxy_launcher_enabled }
    }

    fn contains_peer(&self, id: &PeerId) -> bool {
        let inner = self.inner.lock();
        inner.contains_peer(id)
    }

    fn register_peer(
        &self,
        id: PeerId,
        observer: ClientEnd<bredr::PeerObserverMarker>,
        sender: mpsc::Sender<(PeerId, bredr::ProfileRequestStream)>,
    ) -> Result<(), Error> {
        if self.contains_peer(&id) {
            return Err(format_err!("Peer {} already registered!", id));
        }

        // Each registered peer will have its own ServiceFs and NestedEnvironment. This allows
        // for the sandboxed launching of profiles.
        let mut peer_service_fs = ServiceFs::new();
        let _ = peer_service_fs.add_fidl_service(move |stream| {
            let mut sender_clone = sender.clone();
            fasync::Task::spawn(async move {
                sender_clone
                    .send((id, stream))
                    .await
                    .expect("relaying ProfileRequestStream failed");
            })
            .detach();
        });

        let env = if self.proxy_launcher_enabled {
            let env_name = format!("peer_{}", id);
            let options = EnvironmentOptions {
                inherit_parent_services: true,
                use_parent_runners: false,
                kill_on_oom: false,
                delete_storage_on_death: false,
            };
            Some(
                peer_service_fs
                    .create_nested_environment_with_options(env_name.as_str(), options)?,
            )
        } else {
            None
        };

        let observer = observer.into_proxy()?;
        let mock_peer = MockPeer::new(id, env, Some(observer));

        fasync::Task::spawn(peer_service_fs.collect()).detach();

        // Complete registration by storing the `MockPeer` in the Mock Piconet Server database.
        {
            let mut inner = self.inner.lock();
            inner.register_peer(id, mock_peer)?;
        }

        Ok(())
    }

    fn unregister_peer(&self, id: PeerId) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        inner.unregister_peer(&id)
    }

    fn launch_profile(&self, id: PeerId, launch_info: LaunchInfo) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        inner.launch_profile(id, launch_info)
    }

    fn new_advertisement(
        &self,
        id: PeerId,
        services: Vec<bredr::ServiceDefinition>,
        receiver: bredr::ConnectionReceiverProxy,
        responder: bredr::ProfileAdvertiseResponder,
    ) {
        let mut inner = self.inner.lock();
        inner.new_advertisement(id, services, receiver, responder);
    }

    fn new_connection(
        &self,
        id: PeerId,
        other_id: PeerId,
        connection: bredr::ConnectParameters,
    ) -> Result<bredr::Channel, Error> {
        let mut inner = self.inner.lock();
        inner.new_connection(id, other_id, connection)
    }

    fn new_search(
        &self,
        id: PeerId,
        service_uuid: bredr::ServiceClassProfileIdentifier,
        attr_ids: Vec<u16>,
        results: bredr::SearchResultsProxy,
    ) {
        let mut inner = self.inner.lock();
        inner.new_search(id, service_uuid, attr_ids, results);
    }

    fn handle_profile_request(&self, id: PeerId, request: bredr::ProfileRequest) {
        match request {
            bredr::ProfileRequest::Advertise { services, receiver, responder, .. } => {
                let proxy = receiver.into_proxy().expect("couldn't get connection receiver");
                self.new_advertisement(id, services, proxy, responder);
            }
            bredr::ProfileRequest::Connect { peer_id, connection, responder, .. } => {
                let mut channel = self
                    .new_connection(id, peer_id.into(), connection)
                    .map_err(|_| ErrorCode::Failed);
                let _ = responder.send(&mut channel);
            }
            bredr::ProfileRequest::Search { service_uuid, attr_ids, results, .. } => {
                let proxy = results.into_proxy().expect("couldn't get connection receiver");
                self.new_search(id, service_uuid, attr_ids, proxy);
            }
            bredr::ProfileRequest::ConnectSco { receiver, .. } => {
                let proxy = receiver.into_proxy().expect("couldn't get sco connection receiver");
                let _ = proxy.error(bredr::ScoErrorCode::Failure);
                error!("ConnectSco not implemented");
            }
        }
    }

    async fn handle_mock_peer_request(
        &self,
        id: PeerId,
        request: bredr::MockPeerRequest,
        mut sender: mpsc::Sender<(PeerId, bredr::ProfileRequestStream)>,
    ) {
        match request {
            bredr::MockPeerRequest::ConnectProxy_ { interface, responder, .. } => {
                // Relay the ProfileRequestStream to the central handler.
                match interface.into_stream() {
                    Ok(stream) => {
                        if let Err(e) = sender.send((id, stream)).await {
                            error!("Error relaying ProfileRequestStream: {:?}", e);
                            responder.control_handle().shutdown_with_epitaph(zx::Status::INTERNAL);
                            return;
                        }
                        if let Err(e) = responder.send() {
                            error!("Error sending on responder: {:?}", e);
                        }
                    }
                    Err(e) => {
                        error!("Peer {} unable to connect ProfileProxy: {:?}", id, e);
                        responder.control_handle().shutdown_with_epitaph(zx::Status::BAD_HANDLE);
                    }
                }
            }
            bredr::MockPeerRequest::LaunchProfile { launch_info, responder, .. } => {
                let mut result = match LaunchInfo::try_from(launch_info) {
                    Ok(info) => self.launch_profile(id, info).map_err(|_| ErrorCode::Failed),
                    Err(_) => Err(ErrorCode::InvalidArguments),
                };
                if let Err(e) = responder.send(&mut result) {
                    error!("Error sending on responder: {:?}", e);
                }
            }
        }
    }

    /// Central handler that consumes requests from several sources.
    ///   1. Processes requests over the `bredr.ProfileTest` protocol.
    ///   2. Processes requests over the `bredr.MockPeer` protocol.
    ///   3. Processes requests over the `bredr.Profile` protocol.
    ///   4. Processes messages over a local mpsc channel. This relays the ProfileRequestStream
    ///      that is created when a sandboxed instance of a Bluetooth Profile is launched.
    async fn handle_fidl_requests(
        &self,
        mut profile_test_requests: mpsc::Receiver<bredr::ProfileTestRequest>,
    ) {
        // A combined stream of all the active peers' MockPeerRequestStreams.
        // Each MockPeerRequest is tagged with its corresponding PeerId.
        let mut mock_peer_requests = futures::stream::SelectAll::new();

        // A channel used for relaying the ProfileRequestStream of a peer.
        let (profile_stream_sender, mut profile_stream_receiver) = mpsc::channel(1);

        // A combined stream of all the active peers' ProfileRequestStreams.
        // Each ProfileRequest is tagged with it's corresponding PeerId.
        let mut profile_requests = futures::stream::SelectAll::new();

        loop {
            select! {
                // A request from the `ProfileTest` FIDL request stream has been received.
                test_request = profile_test_requests.select_next_some() => {
                    let bredr::ProfileTestRequest::RegisterPeer { peer_id, peer, observer, responder, .. } = test_request;
                    let id = peer_id.into();
                    let request_stream = match peer.into_stream() {
                        Ok(stream) => stream,
                        Err(_) => {
                            responder.control_handle().shutdown_with_epitaph(zx::Status::BAD_HANDLE);
                            continue;
                        }
                    };
                    let registration =
                        self.register_peer(id, observer, profile_stream_sender.clone());

                    // If registration was successful, tag the MockPeerRequestStream
                    // with the `id` and add to the combinator.
                    match registration {
                        Ok(_) => {
                            mock_peer_requests.push(request_stream.tagged(id).with_epitaph(id));
                        }
                        Err(e) => {
                            error!("Error registering peer {}: {:?}", id, e);
                        }
                    }
                    let _ = responder.send();
                }
                // A request from the `MockPeer` FIDL request stream has been received.
                mock_peer_request = mock_peer_requests.next() => {
                    match mock_peer_request {
                        Some(StreamItem::Item((peer_id, Ok(request)))) => {
                            self.handle_mock_peer_request(peer_id, request, profile_stream_sender.clone()).await;
                        },
                        Some(StreamItem::Item((peer_id, Err(e)))) => {
                            error!("Peer {} received MockPeerRequest error: {:?}", peer_id, e);
                        }
                        Some(StreamItem::Epitaph(peer_id)) => {
                            // The MockPeerRequestStream associated with `peer_id` has been
                            // exhausted, signaled by the epitaph. This means the peer has
                            // disconnected from the piconet.
                            if let Err(e) = self.unregister_peer(peer_id) {
                                error!("Error unregistering peer {}: {:?}", peer_id, e);
                            }
                        },
                        None => (),
                    }
                }
                // A request from the `Profile` FIDL request stream has been received.
                profile_request = profile_requests.next() => {
                    match profile_request {
                        Some(StreamItem::Item((peer_id, request))) => {
                            if let Ok(req) = request {
                                self.handle_profile_request(peer_id, req);
                            }
                        },
                        Some(StreamItem::Epitaph(_)) => {
                        },
                        None => (),
                    }
                }
                // A new ProfileRequestStream has been received. Tag with the relevant PeerId, and
                // add to the Profile handler combinator.
                request_stream = profile_stream_receiver.select_next_some() => {
                    let (id, request_stream) = request_stream;
                    profile_requests.push(request_stream.tagged(id).with_epitaph(id));
                }
                // There are no active streams to be polled.
                complete => break,
            }
        }
    }
}
/// The `MockPiconetServerInner` handles all state bookkeeping for the peers in the piconet.
/// There is one `MockPeer` object, identified by a unique PeerId, for every peer.
/// FIDL requests for a specific Peer will be routed to a peer's `MockPeer`.
pub struct MockPiconetServerInner {
    /// Map of all the peers in the piconet, identified by a unique PeerId.
    peers: HashMap<PeerId, MockPeer>,
}

impl MockPiconetServerInner {
    pub fn new() -> Self {
        Self { peers: HashMap::new() }
    }

    pub fn contains_peer(&self, id: &PeerId) -> bool {
        self.peers.contains_key(id)
    }

    /// Registers a peer in the database.
    ///
    /// `contains_peer()` must be called before using `register_peer()` to
    /// validate that `id` isn't already registered.
    pub fn register_peer(&mut self, id: PeerId, peer: MockPeer) -> Result<(), Error> {
        if self.contains_peer(&id) {
            return Err(format_err!("Peer {} already registered", id));
        }
        let _ = self.peers.insert(id, peer);
        Ok(())
    }

    /// Attempts to unregister a peer from the database.
    ///
    /// Returns an error if the requested `id` doesn't exist.
    pub fn unregister_peer(&mut self, id: &PeerId) -> Result<(), Error> {
        if !self.contains_peer(id) {
            return Err(format_err!("Peer {} doesn't exist", id));
        }
        drop(self.peers.remove(id));
        Ok(())
    }

    /// Attempts to launch a profile, specified by the `profile_url`, for the peer.
    ///
    /// Returns an error if Peer `id` is not registered, or if launching the profile fails.
    pub fn launch_profile(&mut self, id: PeerId, launch_info: LaunchInfo) -> Result<(), Error> {
        match self.peers.entry(id) {
            Entry::Vacant(_) => Err(format_err!("Peer {} not registered.", id)),
            Entry::Occupied(mut entry) => {
                let (_, component_stream) = entry.get_mut().launch_profile(launch_info)?;
                fasync::Task::spawn(async move {
                    component_stream.map(|_| ()).collect::<()>().await;
                })
                .detach();
                Ok(())
            }
        }
    }

    /// Attempts to add a new advertisement for a set of `services` for the peer.
    ///
    /// If the registration of services is successful, attempts to match the newly
    /// added services to any outstanding searches in the piconet.
    pub fn new_advertisement(
        &mut self,
        id: PeerId,
        services: Vec<bredr::ServiceDefinition>,
        receiver: bredr::ConnectionReceiverProxy,
        responder: bredr::ProfileAdvertiseResponder,
    ) {
        let res = match self.peers.entry(id) {
            Entry::Vacant(_) => {
                info!("Peer {} not registered.", id);
                return;
            }
            Entry::Occupied(mut entry) => entry.get_mut().new_advertisement(services, receiver),
        };

        match res {
            Ok((svc_ids, adv_fut)) => {
                fasync::Task::spawn(async move {
                    adv_fut.await;
                    // Reply to the hanging-get responder when the advertisement completes.
                    let _ = responder.send(&mut Ok(()));
                })
                .detach();
                self.find_matching_searches(id, svc_ids);
            }
            Err(e) => info!("Peer {} error advertising service: {:?}", id, e),
        }
    }

    /// Attempts to create a connection between peers specified by `initiator` and `other`.
    pub fn new_connection(
        &mut self,
        initiator: PeerId,
        other: PeerId,
        connection: bredr::ConnectParameters,
    ) -> Result<bredr::Channel, Error> {
        if !self.contains_peer(&initiator) {
            return Err(format_err!("Peer {} is not registered", initiator));
        }

        if initiator == other {
            return Err(format_err!("Cannot establish connection to oneself"));
        }

        // Similar to the bt-host Profile Server, only L2CAP connections can be made.
        // Clients that require RFCOMM channels should register through the `bt-rfcomm` component.
        let psm = match connection {
            bredr::ConnectParameters::L2cap(params) => {
                let psm = params.psm.ok_or(format_err!("No PSM provided in connection"))?;
                Psm::new(psm)
            }
            bredr::ConnectParameters::Rfcomm(_) => return Err(format_err!("RFCOMM not supported")),
        };

        // Attempt to establish a connection between the peers.
        self.peers
            .get(&other)
            .map(|peer| peer.new_connection(initiator, psm))
            .unwrap_or(Err(format_err!("Peer {} is not registered", other)))
    }

    /// Attempts to add a new search for the peer and match the search with any
    /// outstanding service advertisements.
    pub fn new_search(
        &mut self,
        id: PeerId,
        service_uuid: bredr::ServiceClassProfileIdentifier,
        attr_ids: Vec<u16>,
        results: bredr::SearchResultsProxy,
    ) {
        match self.peers.entry(id) {
            Entry::Vacant(_) => {
                info!("Peer {} not registered.", id);
                return;
            }
            Entry::Occupied(mut entry) => {
                let search_fut = entry.get_mut().new_search(service_uuid, attr_ids, results);
                fasync::Task::spawn(search_fut).detach();
            }
        }

        self.find_matching_advertisements(id, service_uuid);
    }

    /// Checks all outstanding advertisements from other mock peers, and attempts to match a
    /// search from peer `id` with an advertisement.
    /// `service_id` is the identifier that the peer is searching for.
    pub fn find_matching_advertisements(
        &mut self,
        peer_id: PeerId,
        service_id: bredr::ServiceClassProfileIdentifier,
    ) {
        let requested_service_ids = vec![service_id].into_iter().collect();

        // Get all active service advertisements in the piconet that match `service_id`.
        let matching_services = self
            .peers
            .iter()
            .filter(|(&id, _)| id != peer_id)
            .filter_map(|(_, peer2)| {
                peer2.get_advertised_services(&requested_service_ids).get(&service_id).cloned()
            })
            .flatten()
            .collect();

        // Update the service searches for peer `peer_id` with the compiled list of service
        // advertisements.
        if let Some(peer) = self.peers.get_mut(&peer_id) {
            peer.notify_searches(&service_id, matching_services);
        }
    }

    /// Checks all outstanding searches from other mock peers, and attempts to match a
    /// service advertisement from peer `id` with a search.
    /// `service_ids` is the set of Service Class Profile Identifiers to match searches for.
    fn find_matching_searches(
        &mut self,
        peer_id: PeerId,
        service_ids: HashSet<bredr::ServiceClassProfileIdentifier>,
    ) {
        // The outstanding advertisements for the peer. It should contain entries
        // for _at least_ the service class IDs in `service_ids`.
        let advertisements = match self.peers.get(&peer_id) {
            Some(peer) => peer.get_advertised_services(&service_ids),
            None => return,
        };

        for (peer_id2, peer2) in self.peers.iter_mut() {
            if &peer_id != peer_id2 {
                let active_searches = peer2.get_active_searches();
                let intersection = service_ids.intersection(&active_searches);
                for id in intersection {
                    peer2
                        .notify_searches(id, advertisements.get(id).expect("should exist").clone());
                }
            }
        }
    }
}

/// Forward requests from the `fuchsia.bluetooth.bredr.ProfileTest` service to the request handler.
async fn handle_test_client_connection(
    mut sender: mpsc::Sender<bredr::ProfileTestRequest>,
    mut stream: bredr::ProfileTestRequestStream,
) {
    while let Some(request) = stream.next().await {
        match request {
            Ok(request) => sender.send(request).await.expect("send to handler failed"),
            Err(e) => error!("Client connection failed: {}", e),
        }
    }
}

#[derive(Debug, FromArgs)]
/// Run the Bluetooth Mock Piconet Server, which is a mock piconet manager.
struct Options {
    #[argh(switch)]
    /// whether we should run in v1 mode. LaunchProfile can only be used with
    /// v1. This defaults to false so the Mock Piconet Server defaults to v2
    /// mode.
    v1: bool,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["bt-mock-piconet-server"])
        .expect("Unable to initialize logger");

    let args: Options = argh::from_env();

    let server = MockPiconetServer::new(args.v1);

    let (test_sender, test_receiver) = mpsc::channel(0);

    let mut fs = ServiceFs::new();
    let _ = fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::spawn(handle_test_client_connection(test_sender.clone(), stream)).detach()
    });
    let _ = fs.take_and_serve_directory_handle()?;
    let drive_service_fs = fs.collect::<()>().fuse();

    let handle_fut = server.handle_fidl_requests(test_receiver);
    futures::pin_mut!(handle_fut);
    let _ = futures::future::select(handle_fut, drive_service_fs).await;

    Ok(())
}

// TODO(fxbug.dev/55461): Add unit tests for the `TestProfileSeverInner`.
#[cfg(test)]
mod tests {
    use super::*;

    use fidl::encoding::Decodable;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream, create_request_stream};
    use fidl_fuchsia_bluetooth_bredr::{
        ChannelParameters, ConnectionReceiverMarker, MockPeerMarker, MockPeerProxy,
        PeerObserverMarker, PeerObserverRequestStream, ProfileMarker, ProfileTestMarker,
        ProfileTestRequest, ProfileTestRequestStream,
    };
    use futures::pin_mut;

    async fn get_next_profile_test_request(
        stream: &mut ProfileTestRequestStream,
    ) -> Result<ProfileTestRequest, Error> {
        stream.select_next_some().await.map_err(|e| format_err!("{:?}", e))
    }

    fn generate_register_peer_request(
        exec: &mut fasync::TestExecutor,
        id: PeerId,
    ) -> (MockPeerProxy, PeerObserverRequestStream, ProfileTestRequest) {
        // Used to simulate behavior of an integration test client. Sends
        // requests using the ProfileTest interface.
        let (client, mut server) = create_proxy_and_stream::<ProfileTestMarker>().unwrap();

        let (mock_peer, mock_peer_server) = create_proxy::<MockPeerMarker>().unwrap();
        let (observer, observer_stream) = create_request_stream::<PeerObserverMarker>().unwrap();
        let reg_fut = client.register_peer(&mut id.into(), mock_peer_server, observer);
        pin_mut!(reg_fut);

        assert!(exec.run_until_stalled(&mut reg_fut).is_pending());

        let req_fut = get_next_profile_test_request(&mut server);
        pin_mut!(req_fut);
        let request = exec.run_singlethreaded(&mut req_fut).unwrap();

        (mock_peer, observer_stream, request)
    }

    #[test]
    fn test_register_peer() -> Result<(), Error> {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let mps = MockPiconetServer::new(true);
        let (mut sender, receiver) = mpsc::channel(0);

        // The main handler - this is under test.
        let mps_fut = mps.handle_fidl_requests(receiver);
        pin_mut!(mps_fut);
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());

        // Register a mock peer.
        let id = PeerId(123);
        let (mock_peer, _observer, request) = generate_register_peer_request(&mut exec, id);

        // Forward the request to the handler. After running the main `mps_fut`, the peer
        // should be registered.
        assert!(exec.run_until_stalled(&mut sender.send(request)).is_pending());
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());
        assert!(mps.contains_peer(&id));

        // Dropping the MockPeer client should simulate peer disconnection.
        drop(mock_peer);
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());
        assert!(!mps.contains_peer(&id));

        Ok(())
    }

    #[test]
    fn test_advertisement_request_resolves_when_terminated() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let mps = MockPiconetServer::new(true);
        let (mut sender, receiver) = mpsc::channel(0);

        // The main handler - this is under test.
        let mps_fut = mps.handle_fidl_requests(receiver);
        pin_mut!(mps_fut);
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());

        // Register a mock peer.
        let id = PeerId(123);
        let (mock_peer, _observer, request) = generate_register_peer_request(&mut exec, id);

        // Forward the request to the handler. After running the main `mps_fut`, the peer
        // should be registered.
        assert!(exec.run_until_stalled(&mut sender.send(request)).is_pending());
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());

        // Connect the ProfileProxy.
        let (c, s) = create_proxy::<ProfileMarker>().unwrap();
        let connect_fut = mock_peer.connect_proxy_(s);
        pin_mut!(connect_fut);
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());
        assert!(exec.run_until_stalled(&mut connect_fut).is_ready());

        // Advertise - the hanging-get request shouldn't resolve.
        let (target, receiver) = create_request_stream::<ConnectionReceiverMarker>().unwrap();
        let services = vec![];
        let mut adv_fut =
            c.advertise(&mut services.into_iter(), ChannelParameters::new_empty(), target);
        assert!(exec.run_until_stalled(&mut adv_fut).is_pending());
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());

        // We decide to stop advertising.
        drop(receiver);
        assert!(exec.run_until_stalled(&mut mps_fut).is_pending());
        assert!(exec.run_until_stalled(&mut adv_fut).is_ready());
    }
}
