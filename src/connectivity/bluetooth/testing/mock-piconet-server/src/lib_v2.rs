// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::peer_observer_timeout,
    anyhow::{format_err, Context, Error},
    cm_rust::{ComponentDecl, ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget},
    fidl::{
        encoding::Decodable,
        endpoints::{self as f_end, DiscoverableProtocolMarker},
    },
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_logger::LogSinkMarker,
    fuchsia_async::{self as fasync, futures::TryStreamExt, DurationExt, TimeoutExt},
    fuchsia_bluetooth::types as bt_types,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        mock, Moniker, Realm, RealmInstance,
    },
    fuchsia_zircon as zx,
    futures::{stream::StreamExt, TryFutureExt},
};

static MOCK_PICONET_SERVER_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/mock-piconet-server#meta/mock-piconet-server-v2.cm";
static PROFILE_INTERPOSER_PREFIX: &str = "profile-interposer";
static BT_RFCOMM_PREFIX: &str = "bt-rfcomm";

/// Specification data for creating a peer in the mock piconet. This may be a
/// peer that will be driven by test code or one that is an actual bluetooth
/// profile implementation.
#[derive(Clone, Debug)]
pub struct PiconetMemberSpec {
    pub name: String,
    pub id: bt_types::PeerId,
    /// The optional hermetic RFCOMM component URL to be used by piconet members
    /// that need RFCOMM functionality.
    rfcomm_url: Option<String>,
    observer: Option<bredr::PeerObserverProxy>,
}

impl PiconetMemberSpec {
    pub fn get_profile_proxy(
        &self,
        topology: &RealmInstance,
    ) -> Result<bredr::ProfileProxy, anyhow::Error> {
        let (client, server) = f_end::create_endpoints::<bredr::ProfileMarker>()?;
        topology.root.connect_request_to_named_protocol_at_exposed_dir(
            &mock_profile_service_path(self),
            server.into_channel(),
        )?;
        client.into_proxy().map_err(|e| e.into())
    }

    /// Create a PiconetMemberSpec configured to be used with a Profile
    /// component which is under test.
    /// `rfcomm_url` is the URL for an optional v2 RFCOMM component that will sit between the
    /// Profile and the Mock Piconet Server.
    pub fn for_profile(
        name: String,
        rfcomm_url: Option<String>,
    ) -> (Self, bredr::PeerObserverRequestStream) {
        let (peer_proxy, peer_stream) =
            f_end::create_proxy_and_stream::<bredr::PeerObserverMarker>().unwrap();

        (
            Self { name, id: bt_types::PeerId::random(), observer: Some(peer_proxy), rfcomm_url },
            peer_stream,
        )
    }

    /// Create a PiconetMemberSpec designed to be used with a peer that will be driven
    /// by test code.
    /// `rfcomm_url` is the URL for an optional v2 RFCOMM component that will sit between the
    /// mock peer and the Mock Piconet Server.
    pub fn for_mock_peer(name: String, rfcomm_url: Option<String>) -> Self {
        Self { name, id: bt_types::PeerId::random(), rfcomm_url, observer: None }
    }
}

fn mock_profile_service_path(mock: &PiconetMemberSpec) -> String {
    format!("{}-{}", bredr::ProfileMarker::PROTOCOL_NAME, mock.id)
}

pub struct PiconetMember {
    id: bt_types::PeerId,
    profile_svc: bredr::ProfileProxy,
}

impl PiconetMember {
    pub fn peer_id(&self) -> bt_types::PeerId {
        self.id
    }

    pub fn new_from_spec(
        mock: PiconetMemberSpec,
        realm: &RealmInstance,
    ) -> Result<Self, anyhow::Error> {
        Ok(Self {
            id: mock.id,
            profile_svc: mock
                .get_profile_proxy(realm)
                .context("failed to open mock's profile proxy")?,
        })
    }

    /// Register a service search using the Profile protocol for services that match `svc_id`.
    ///
    /// Returns a stream of search results that can be polled to receive new requests.
    pub fn register_service_search(
        &self,
        svc_id: bredr::ServiceClassProfileIdentifier,
        attributes: Vec<u16>,
    ) -> Result<bredr::SearchResultsRequestStream, Error> {
        let (results_client, results_requests) =
            f_end::create_request_stream().expect("couldn't create endpoints");
        self.profile_svc.search(svc_id, &attributes, results_client)?;
        Ok(results_requests)
    }

    /// Register a service advertisement using the Profile protocol with the provided
    /// `service_defs`.
    ///
    /// Returns a stream of connection requests that can be polled to receive new requests.
    pub fn register_service_advertisement(
        &self,
        service_defs: Vec<bredr::ServiceDefinition>,
    ) -> Result<bredr::ConnectionReceiverRequestStream, Error> {
        let (connect_client, connect_requests) =
            f_end::create_request_stream().context("ConnectionReceiver creation")?;
        let _ = self.profile_svc.advertise(
            &mut service_defs.into_iter(),
            bredr::ChannelParameters::new_empty(),
            connect_client,
        );

        Ok(connect_requests)
    }

    pub async fn make_connection(
        &self,
        peer_id: bt_types::PeerId,
        mut params: bredr::ConnectParameters,
    ) -> Result<bredr::Channel, Error> {
        self.profile_svc
            .connect(&mut peer_id.into(), &mut params)
            .await?
            .map_err(|e| format_err!("{:?}", e))
    }
}

/// Helper designed to observe what a real profile implementation is doing.
pub struct ProfileObserver {
    observer_stream: bredr::PeerObserverRequestStream,
    profile_id: bt_types::PeerId,
}

impl ProfileObserver {
    pub fn new(stream: bredr::PeerObserverRequestStream, id: bt_types::PeerId) -> Self {
        Self { observer_stream: stream, profile_id: id }
    }

    pub fn peer_id(&self) -> bt_types::PeerId {
        self.profile_id
    }

    /// Expects a request over the PeerObserver protocol for this MockPeer.
    ///
    /// Returns the request if successful.
    pub async fn expect_observer_request(&mut self) -> Result<bredr::PeerObserverRequest, Error> {
        // The Future is gated by a timeout so that tests consistently terminate.
        self.observer_stream
            .select_next_some()
            .map_err(|e| format_err!("{:?}", e))
            .on_timeout(peer_observer_timeout().after_now(), move || {
                Err(format_err!("observer timed out"))
            })
            .await
    }

    /// Expects a connection request between the profile under test and the `other` peer.
    ///
    /// Returns Ok on success, Error if there was no connection request on the observer or
    /// the request was for a different peer.
    pub async fn expect_observer_connection_request(
        &mut self,
        other: bt_types::PeerId,
    ) -> Result<(), Error> {
        let request = self.expect_observer_request().await?;
        match request {
            bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
                responder.send().unwrap();
                if other == peer_id.into() {
                    Ok(())
                } else {
                    Err(format_err!("Connection request for unexpected peer: {:?}", peer_id))
                }
            }
            x => Err(format_err!("Expected PeerConnected but got: {:?}", x)),
        }
    }

    /// Expects the profile under test to discover the services of the `other` peer.
    ///
    /// Returns Ok on success, Error if there was no ServiceFound request on the observer or
    /// the request was for a different peer.
    pub async fn expect_observer_service_found_request(
        &mut self,
        other: bt_types::PeerId,
    ) -> Result<(), Error> {
        let request = self.expect_observer_request().await?;
        match request {
            bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } => {
                responder.send().unwrap();
                if other == peer_id.into() {
                    Ok(())
                } else {
                    Err(format_err!("ServiceFound request for unexpected peer: {:?}", peer_id))
                }
            }
            x => Err(format_err!("Expected PeerConnected but got: {:?}", x)),
        }
    }
}

/// Adds a profile to the test topology.
///
/// This also creates a component that sits between the profile and the Mock Piconet Server.
/// This node acts as a facade between the profile under test and the Server.
/// If the `spec` contains a channel then `PeerObserver` events will be forwarded to that
/// channel.
/// `additional_capabilities` specifies capability routings for any protocols used/exposed
/// by the profile.
async fn add_profile_to_topology<'a>(
    builder: &mut RealmBuilder,
    spec: &'a mut PiconetMemberSpec,
    server_moniker: String,
    profile_url: String,
    additional_capabilities: Vec<CapabilityRoute>,
) -> Result<(), Error> {
    // Specify the interposer component that will provide `Profile` to the profile under test.
    let mock_piconet_member_name = interposer_name_for_profile(&spec.name);
    add_mock_piconet_component(
        builder,
        mock_piconet_member_name.clone(),
        spec.id,
        bredr::ProfileMarker::PROTOCOL_NAME.to_string(),
        spec.observer.take(),
    )
    .await?;

    // If required, specify the RFCOMM intermediary component.
    let rfcomm_moniker = bt_rfcomm_moniker_for_member(&spec.name);
    if let Some(url) = &spec.rfcomm_url {
        add_bt_rfcomm_intermediary(builder, rfcomm_moniker.clone(), url.clone()).await?;
    }

    // Specify the profile under test.
    {
        let _ = builder
            .add_eager_component(spec.name.to_string(), ComponentSource::Url(profile_url))
            .await?;
    }

    // Capability routes:
    //   * If `bt-rfcomm` is specified as an intermediary, `Profile` from mock piconet member
    //     to `bt-rfcomm` and then from `bt-rfcomm` to profile under test.
    //     Otherwise, `Profile` directly from mock piconet member to profile under test.
    //   * `ProfileTest` from Mock Piconet Server to mock piconet member
    //   * `LogSink` from parent to the profile under test & mock piconet member.
    //   * Additional capabilities from the profile under test to AboveRoot to be
    //     accessible via the test realm service directory.
    {
        if spec.rfcomm_url.is_some() {
            builder.add_protocol_route::<bredr::ProfileMarker>(
                RouteEndpoint::component(mock_piconet_member_name.clone()),
                vec![RouteEndpoint::component(rfcomm_moniker.clone())],
            )?;
            builder.add_protocol_route::<bredr::ProfileMarker>(
                RouteEndpoint::component(rfcomm_moniker.clone()),
                vec![RouteEndpoint::component(spec.name.to_string())],
            )?;
        } else {
            builder.add_protocol_route::<bredr::ProfileMarker>(
                RouteEndpoint::component(mock_piconet_member_name.clone()),
                vec![RouteEndpoint::component(spec.name.to_string())],
            )?;
        }

        builder.add_protocol_route::<bredr::ProfileTestMarker>(
            RouteEndpoint::component(&server_moniker),
            vec![RouteEndpoint::component(mock_piconet_member_name.clone())],
        )?;

        builder.add_protocol_route::<LogSinkMarker>(
            RouteEndpoint::AboveRoot,
            vec![
                RouteEndpoint::component(spec.name.to_string()),
                RouteEndpoint::component(mock_piconet_member_name.clone()),
            ],
        )?;

        for route in additional_capabilities {
            builder.add_route(route)?;
        }
    }
    Ok(())
}

async fn add_mock_piconet_member<'a, 'b>(
    builder: &mut RealmBuilder,
    mock: &'a mut PiconetMemberSpec,
    server_moniker: String,
) -> Result<(), Error> {
    // The capability path of `Profile` is determined by the existence of the RFCOMM intermediary.
    // - If the RFCOMM intermediary is specified, the mock piconet component will expose `Profile`
    //   to the RFCOMM intermediary at the standard path. The RFCOMM intermediary will then expose
    //   `Profile` at a unique path.
    // - If the RFCOMM intermediary is not specified, the mock piconet component will directly
    //   expose `Profile` at a unique path.
    let profile_path = if mock.rfcomm_url.is_some() {
        bredr::ProfileMarker::PROTOCOL_NAME.to_string()
    } else {
        mock_profile_service_path(mock)
    };
    add_mock_piconet_component(
        builder,
        mock.name.to_string(),
        mock.id,
        profile_path.clone(),
        mock.observer.take(),
    )
    .await?;

    // If required, specify the RFCOMM intermediary component.
    let rfcomm_moniker = bt_rfcomm_moniker_for_member(&mock.name);
    if let Some(url) = &mock.rfcomm_url {
        add_bt_rfcomm_intermediary(builder, rfcomm_moniker.clone(), url.clone()).await?;
    }

    // Capability routes:
    // - `ProfileTest` from the Mock Piconet Server to the mock piconet member component.
    // - If `bt-rfcomm` is specified as an intermediary, `Profile` from mock piconet member
    //   to `bt-rfcomm`.
    //   Note: Exposing `Profile` from `bt-rfcomm` to AboveRoot at a unique path will happen after
    //   the test realm has been defined and built due to constraints of component decls.
    // - If `bt-rfcomm` is not specified, route `Profile` directly from mock piconet member to
    //   AboveRoot at the unique path (e.g fuchsia.bluetooth.bredr.Profile-3 where "3" is the peer
    //   ID of the mock).
    {
        let _ = builder.add_protocol_route::<bredr::ProfileTestMarker>(
            RouteEndpoint::component(&server_moniker),
            vec![RouteEndpoint::component(mock.name.clone())],
        )?;

        if mock.rfcomm_url.is_some() {
            builder.add_route(CapabilityRoute {
                capability: Capability::protocol(profile_path),
                source: RouteEndpoint::component(mock.name.clone()),
                targets: vec![RouteEndpoint::component(rfcomm_moniker.clone())],
            })?;
        } else {
            builder.add_route(CapabilityRoute {
                capability: Capability::protocol(profile_path),
                source: RouteEndpoint::component(mock.name.to_string()),
                targets: vec![RouteEndpoint::AboveRoot],
            })?;
        }
    }

    Ok(())
}

/// Add the mock piconet member to the Realm. If observer_src is None a channel
/// is created and the server end shipped off to a future to be drained.
async fn add_mock_piconet_component(
    builder: &mut RealmBuilder,
    name: String,
    id: bt_types::PeerId,
    profile_svc_path: String,
    observer_src: Option<bredr::PeerObserverProxy>,
) -> Result<(), Error> {
    // If there is no observer, make a channel and fill it in. The server end
    // of the channel is passed to a future which just reads the channel to
    // completion.
    let observer = observer_src.unwrap_or_else(|| {
        let (proxy, stream) =
            f_end::create_proxy_and_stream::<bredr::PeerObserverMarker>().unwrap();
        fasync::Task::local(async move {
            let _ = drain_observer(stream).await;
        })
        .detach();
        proxy
    });

    builder
        .add_component(
            name,
            ComponentSource::Mock(mock::Mock::new(move |m: mock::MockHandles| {
                let observer = observer.clone();
                Box::pin(piconet_member(m, id, profile_svc_path.clone(), observer))
            })),
        )
        .await
        .map(|_| ())
        .map_err(|e| e.into())
}

/// Drives the mock piconet member. This receives the open request for the
/// Profile service, attaches it to the Mock Piconet Server, and wires up the
/// the PeerObserver.
async fn piconet_member(
    handles: mock::MockHandles,
    id: bt_types::PeerId,
    profile_svc_path: String,
    peer_observer: bredr::PeerObserverProxy,
) -> Result<(), Error> {
    // connect to the profile service to drive the mock peer
    let pro_test = handles.connect_to_service::<bredr::ProfileTestMarker>()?;
    let mut fs = ServiceFs::new();

    fs.dir("svc").add_service_at(profile_svc_path, move |chan: zx::Channel| {
        let profile_test = pro_test.clone();
        let observer = peer_observer.clone();

        fasync::Task::local(async move {
            let (client, observer_req_stream) =
                register_piconet_member(&profile_test, id).await.unwrap();

            client.connect_proxy_(chan.into()).await.expect("failed to get mock peer service");
            // keep us running and hold on until termination to keep the mock alive
            fwd_observer_callbacks(observer_req_stream, &observer, id).await.unwrap();
        })
        .detach();
        Some(())
    });

    fs.serve_connection(handles.outgoing_dir.into_channel()).expect("failed to serve service fs");
    fs.collect::<()>().await;

    Ok(())
}

/// Use the ProfileTestProxy to register a piconet member with the Bluetooth Profile Test
/// Server.
async fn register_piconet_member(
    profile_test_proxy: &bredr::ProfileTestProxy,
    id: bt_types::PeerId,
) -> Result<(bredr::MockPeerProxy, bredr::PeerObserverRequestStream), Error> {
    let (client, server) = f_end::create_proxy::<bredr::MockPeerMarker>()?;
    let (observer_client, observer_server) =
        f_end::create_request_stream::<bredr::PeerObserverMarker>()?;

    profile_test_proxy
        .register_peer(&mut id.into(), server, observer_client)
        .await
        .context("registering peer failed!")?;
    Ok((client, observer_server))
}

fn handle_fidl_err(fidl_err: fidl::Error, ctx: String) -> Result<(), Error> {
    if fidl_err.is_closed() {
        Ok(())
    } else {
        Err(anyhow::Error::from(fidl_err).context(ctx))
    }
}

/// Given a request stream and a proxy, forward from one to the other
async fn fwd_observer_callbacks(
    mut source_req_stream: bredr::PeerObserverRequestStream,
    observer: &bredr::PeerObserverProxy,
    id: bt_types::PeerId,
) -> Result<(), Error> {
    while let Some(req) =
        source_req_stream.try_next().await.context("reading peer observer failed")?
    {
        match req {
            bredr::PeerObserverRequest::ServiceFound {
                mut peer_id,
                protocol,
                mut attributes,
                responder,
            } => {
                let mut proto = match protocol {
                    Some(desc) => desc,
                    None => vec![],
                };

                observer
                    .service_found(
                        &mut peer_id,
                        Some(&mut proto.iter_mut()),
                        &mut attributes.iter_mut(),
                    )
                    .await
                    .or_else(|e| {
                        handle_fidl_err(
                            e,
                            format!("unexpected error forwarding observer event for: {}", id),
                        )
                    })?;

                responder.send().or_else(|e| {
                    handle_fidl_err(
                        e,
                        format!("unexpected error acking observer event for: {}", id),
                    )
                })?;
            }

            bredr::PeerObserverRequest::PeerConnected { mut peer_id, mut protocol, responder } => {
                observer.peer_connected(&mut peer_id, &mut protocol.iter_mut()).await.or_else(
                    |e| {
                        handle_fidl_err(
                            e,
                            format!("unexpected error forwarding observer event for: {}", id),
                        )
                    },
                )?;
                responder.send().or_else(|e| {
                    handle_fidl_err(
                        e,
                        format!("unexpected error acking observer event for: {}", id),
                    )
                })?;
            }
            bredr::PeerObserverRequest::ComponentTerminated { component_url, responder } => {
                observer.component_terminated(&component_url).await.or_else(|e| {
                    handle_fidl_err(
                        e,
                        format!("unexpected error forwarding observer event for: {}", id),
                    )
                })?;
                responder.send().or_else(|e| {
                    handle_fidl_err(
                        e,
                        format!("unexpected error acking observer event for: {}", id),
                    )
                })?;
            }
        }
    }
    Ok(())
}

async fn drain_observer(mut stream: bredr::PeerObserverRequestStream) -> Result<(), fidl::Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            bredr::PeerObserverRequest::ServiceFound { responder, .. } => {
                responder.send()?;
            }
            bredr::PeerObserverRequest::PeerConnected { responder, .. } => {
                responder.send()?;
            }
            bredr::PeerObserverRequest::ComponentTerminated { responder, .. } => {
                responder.send()?;
            }
        }
    }
    Ok(())
}

async fn add_mock_piconet_server(builder: &mut RealmBuilder) -> String {
    let name = mock_piconet_server_moniker().to_string();

    builder
        .add_component(name.clone(), ComponentSource::url(MOCK_PICONET_SERVER_URL_V2))
        .await
        .expect("failed to add");

    builder
        .add_protocol_route::<LogSinkMarker>(
            RouteEndpoint::AboveRoot,
            vec![RouteEndpoint::Component(name.clone())],
        )
        .unwrap();
    name
}

/// Adds the `bt-rfcomm` component, identified by the `url`, to the component topology.
async fn add_bt_rfcomm_intermediary(
    builder: &mut RealmBuilder,
    moniker: String,
    url: String,
) -> Result<(), Error> {
    builder.add_eager_component(moniker.clone(), ComponentSource::url(url)).await?;

    builder.add_protocol_route::<LogSinkMarker>(
        RouteEndpoint::AboveRoot,
        vec![RouteEndpoint::Component(moniker)],
    )?;
    Ok(())
}

fn mock_piconet_server_moniker() -> Moniker {
    vec!["mock-piconet-server".to_string()].into()
}

fn bt_rfcomm_moniker_for_member(member_name: &'_ str) -> String {
    format!("{}-for-{}", BT_RFCOMM_PREFIX, member_name)
}

fn interposer_name_for_profile(profile_name: &'_ str) -> String {
    format!("{}-{}", PROFILE_INTERPOSER_PREFIX, profile_name)
}

/// Represents the topology of a piconet set up by an integration test.
///
/// Provides an API to add members to the piconet, define Bluetooth profiles to be run
/// under test, and specify capability routing in the topology. Bluetooth profiles
/// specified in the topology _must_ be v2 components.
///
/// ### Example Usage:
///
/// let harness = PiconetHarness::new().await;
///
/// // Add a mock piconet member to be driven by test code.
/// let spec = harness.add_mock_piconet_member("mock-peer".to_string()).await?;
/// // Add a Bluetooth Profile (AVRCP) to the topology.
/// let profile_observer = harness.add_profile("bt-avrcp-profile", AVRCP_URL_V2).await?;
///
/// // The topology has been defined and can be built. After this step, it cannot be
/// // modified (e.g Can't add a new mock piconet member).
/// let test_topology = test_harness.build().await?;
///
/// // Get the test-driven peer from the topology.
/// let test_driven_peer = PiconetMember::new_from_spec(spec, &test_topology)?;
///
/// // Manipulate the test-driven peer to indirectly interact with the profile-under-test.
/// let search_results = test_driven_peer.register_service_search(..)?;
/// // Expect some behavior from the profile-under-test.
/// let req = profile_observer.expect_observer_request().await?;
/// assert_eq!(req, ..);
pub struct PiconetHarness {
    pub builder: RealmBuilder,
    pub ps_moniker: String,
    profiles: Vec<String>,
    piconet_members: Vec<PiconetMemberSpec>,
}

impl PiconetHarness {
    pub async fn new() -> Self {
        let mut builder = RealmBuilder::new().await.expect("Couldn't create realm builder");
        let ps_moniker = add_mock_piconet_server(&mut builder).await;
        PiconetHarness { builder, ps_moniker, profiles: Vec::new(), piconet_members: Vec::new() }
    }

    pub async fn add_mock_piconet_members(
        &mut self,
        mocks: &'_ mut Vec<PiconetMemberSpec>,
    ) -> Result<(), Error> {
        for mock in mocks {
            self.add_mock_piconet_member_from_spec(mock).await?;
        }
        Ok(())
    }

    pub async fn add_mock_piconet_member(
        &mut self,
        name: String,
        rfcomm_url: Option<String>,
    ) -> Result<PiconetMemberSpec, Error> {
        let mut mock = PiconetMemberSpec::for_mock_peer(name, rfcomm_url);

        self.add_mock_piconet_member_from_spec(&mut mock).await?;
        Ok(mock)
    }

    async fn add_mock_piconet_member_from_spec(
        &mut self,
        mock: &'_ mut PiconetMemberSpec,
    ) -> Result<(), Error> {
        add_mock_piconet_member(&mut self.builder, mock, self.ps_moniker.clone()).await?;
        self.piconet_members.push(mock.clone());
        Ok(())
    }

    /// Potentially updates the `root_decl` with expose decls for piconet `members` that
    /// use an RFCOMM intermediary for the `Profile` capability.
    /// The added expose decls will be defined such that each `Profile` capability will be uniquely
    /// associated with the piconet member.
    async fn expose_rfcomm_profile_capability(
        topology: &mut Realm,
        root_decl: &mut ComponentDecl,
        members: &Vec<PiconetMemberSpec>,
    ) {
        for member_spec in members {
            let rfcomm_name = bt_rfcomm_moniker_for_member(&member_spec.name);
            if let Ok(true) = topology.contains(&rfcomm_name.clone().into()).await {
                root_decl.exposes.push(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Child(rfcomm_name),
                    source_name: bredr::ProfileMarker::PROTOCOL_NAME.into(),
                    target: ExposeTarget::Parent,
                    target_name: mock_profile_service_path(member_spec).into(),
                }))
            }
        }
    }

    /// Builds the test topology with the updated routes for the `bredr.Profile` capability and
    /// returns the built Realm.
    async fn update_routes_and_build(self) -> Result<Realm, Error> {
        let mut topology = self.builder.build();
        log::info!(
            "Building test realm with profiles: {:?}, piconet members: {:?}",
            self.profiles,
            self.piconet_members
        );
        let mut root_decl = topology.get_decl(&Moniker::root()).await.expect("failed to get root");

        // This allows integration test writers to uniquely access the `Profile` capability for each
        // piconet member.
        PiconetHarness::expose_rfcomm_profile_capability(
            &mut topology,
            &mut root_decl,
            &self.piconet_members,
        )
        .await;

        // Update the root decl with the modified `expose` routes.
        topology
            .set_component(&Moniker::root(), root_decl)
            .await
            .expect("Should be able to set root decl");
        Ok(topology)
    }

    pub async fn build(self) -> Result<RealmInstance, Error> {
        let topology = self.update_routes_and_build().await?;
        topology.create().await.map_err(|e| e.into())
    }

    /// Add a profile with moniker `name` to the test topology. The profile should be
    /// accessible via the provided `profile_url` and will be launched during the test.
    ///
    /// Returns an observer for the launched profile.
    pub async fn add_profile(
        &mut self,
        name: String,
        profile_url: String,
    ) -> Result<ProfileObserver, Error> {
        self.add_profile_with_capabilities(name, profile_url, None, vec![], vec![]).await
    }

    /// Add a profile with moniker `name` to the test topology.
    ///
    /// `profile_url` specifies the component URL of the profile under test.
    /// `rfcomm_url` specifies the optional hermetic RFCOMM component URL to be used as an
    /// intermediary in the test topology.
    /// `use_capabilities` specifies any capabilities used by the profile that will be
    /// provided outside the test realm.
    /// `expose_capabilities` specifies any capabilities provided by the profile to be
    /// available in the outgoing directory of the test realm root.
    ///
    /// Returns an observer for the launched profile.
    pub async fn add_profile_with_capabilities(
        &mut self,
        name: String,
        profile_url: String,
        rfcomm_url: Option<String>,
        use_capabilities: Vec<Capability>,
        expose_capabilities: Vec<Capability>,
    ) -> Result<ProfileObserver, Error> {
        let (mut spec, request_stream) = PiconetMemberSpec::for_profile(name, rfcomm_url);
        let mut caps = routes_from_capabilities(
            use_capabilities,
            RouteEndpoint::AboveRoot,
            vec![RouteEndpoint::Component(spec.name.clone())],
        );
        caps.extend(routes_from_capabilities(
            expose_capabilities,
            RouteEndpoint::Component(spec.name.clone()),
            vec![RouteEndpoint::AboveRoot],
        ));

        self.add_profile_from_spec(&mut spec, profile_url, caps).await?;
        Ok(ProfileObserver::new(request_stream, spec.id))
    }

    async fn add_profile_from_spec(
        &mut self,
        spec: &mut PiconetMemberSpec,
        profile_url: String,
        capabilities: Vec<CapabilityRoute>,
    ) -> Result<(), Error> {
        add_profile_to_topology(
            &mut self.builder,
            spec,
            self.ps_moniker.clone(),
            profile_url,
            capabilities,
        )
        .await?;
        self.profiles.push(spec.name.clone());
        Ok(())
    }
}

/// Builds a set of capability routes from `capabilities` that will be routed from
/// `source` to the `targets`.
pub fn routes_from_capabilities(
    capabilities: Vec<Capability>,
    source: RouteEndpoint,
    targets: Vec<RouteEndpoint>,
) -> Vec<CapabilityRoute> {
    capabilities
        .into_iter()
        .map(|capability| CapabilityRoute {
            capability,
            source: source.clone(),
            targets: targets.clone(),
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_rust::{
            CapabilityName, CapabilityPath, DependencyType, ExposeDecl, ExposeProtocolDecl,
            ExposeSource, ExposeTarget, OfferDecl, OfferProtocolDecl, OfferSource, OfferTarget,
            UseDecl, UseProtocolDecl, UseSource,
        },
        fuchsia_component_test::Realm,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_profile_server_added() {
        let test_harness = PiconetHarness::new().await;
        let topology = test_harness.update_routes_and_build().await.expect("should build");
        let mps = topology
            .contains(&super::mock_piconet_server_moniker())
            .await
            .expect("failed to check realm contents");
        assert!(mps);
        topology.create().await.expect("build failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_piconet_member() {
        let mut test_harness = PiconetHarness::new().await;
        let member_name = "test-piconet-member";
        let member_spec = test_harness
            .add_mock_piconet_member(member_name.to_string(), None)
            .await
            .expect("failed to add piconet member");
        assert_eq!(member_spec.name, member_name);

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");
        validate_mock_piconet_member(&mut topology, &member_spec).await;
        let _profile_test_offer = topology.create().await.expect("build failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_piconet_member_with_rfcomm() {
        let mut test_harness = PiconetHarness::new().await;
        let member_name = "test-piconet-member";
        let rfcomm_url = "fuchsia-pkg://fuchsia.com/example#meta/bt-rfcomm.cm".to_string();
        let member_spec = test_harness
            .add_mock_piconet_member(member_name.to_string(), Some(rfcomm_url))
            .await
            .expect("failed to add piconet member");
        assert_eq!(member_spec.name, member_name);

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");
        validate_mock_piconet_member(&mut topology, &member_spec).await;

        // Note: We don't `create()` the test realm because the `rfcomm_url` does not exist which
        // will cause component resolving to fail.
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_multiple_piconet_members() {
        let mut test_harness = PiconetHarness::new().await;
        let member1_name = "test-piconet-member".to_string();
        let member2_name = "test-piconet-member-two".to_string();
        let mut members = vec![
            PiconetMemberSpec::for_mock_peer(member1_name, None),
            PiconetMemberSpec::for_mock_peer(member2_name, None),
        ];

        test_harness
            .add_mock_piconet_members(&mut members)
            .await
            .expect("failed to add piconet members");

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");

        for member in &members {
            validate_mock_piconet_member(&mut topology, member).await;
        }
        let _profile_test_offer = topology.create().await.expect("build failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_multiple_piconet_members_with_rfcomm() {
        let mut test_harness = PiconetHarness::new().await;
        let rfcomm_url = "fuchsia-pkg://fuchsia.com/example#meta/bt-rfcomm.cm".to_string();
        let member1_name = "test-piconet-member-1".to_string();
        let member2_name = "test-piconet-member-2".to_string();
        let member3_name = "test-piconet-member-3".to_string();
        // A combination of RFCOMM and non-RFCOMM piconet members is OK.
        let mut members = vec![
            PiconetMemberSpec::for_mock_peer(member1_name, None),
            PiconetMemberSpec::for_mock_peer(member2_name, Some(rfcomm_url.clone())),
            PiconetMemberSpec::for_mock_peer(member3_name, Some(rfcomm_url)),
        ];

        test_harness
            .add_mock_piconet_members(&mut members)
            .await
            .expect("failed to add piconet members");

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");

        for member in &members {
            validate_mock_piconet_member(&mut topology, member).await;
        }

        // Note: We don't `create()` the test realm because the `rfcomm_url` does not exist which
        // will cause component resolving to fail.
    }

    #[track_caller]
    async fn validate_profile_routes_for_member_with_rfcomm<'a>(
        topology: &mut Realm,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // Piconet member should have an expose declaration of Profile.
        let profile_capability_name = bredr::ProfileMarker::PROTOCOL_NAME.to_string();
        let pico_member_moniker = vec![member_spec.name.to_string()].into();
        let pico_member_decl =
            topology.get_decl(&pico_member_moniker).await.expect("piconet member had no decl");
        let expose_profile_decl = ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: CapabilityName(profile_capability_name.clone()),
            target: ExposeTarget::Parent,
            target_name: CapabilityName(profile_capability_name.clone()),
        };
        let expose_decl = ExposeDecl::Protocol(expose_profile_decl.clone());
        assert!(pico_member_decl.exposes.contains(&expose_decl));

        // Root should have an expose declaration for `Profile` at the custom path.
        {
            let bt_rfcomm_name = super::bt_rfcomm_moniker_for_member(&member_spec.name);
            let custom_profile_capability_name =
                CapabilityName(super::mock_profile_service_path(&member_spec));
            let custom_expose_profile_decl = ExposeProtocolDecl {
                source: ExposeSource::Child(bt_rfcomm_name),
                source_name: CapabilityName(profile_capability_name.to_string()),
                target: ExposeTarget::Parent,
                target_name: CapabilityName(custom_profile_capability_name.to_string()),
            };
            let root_expose_decl = ExposeDecl::Protocol(custom_expose_profile_decl);
            let root = topology.get_decl(&vec![].into()).await.expect("failed to get root");
            assert!(root.exposes.contains(&root_expose_decl));
        }
    }

    #[track_caller]
    async fn validate_profile_routes_for_member<'a>(
        topology: &mut Realm,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // Check that the mock piconet member has an expose declaration for the profile protocol
        let pico_member_moniker = vec![member_spec.name.to_string()].into();
        let pico_member_decl =
            topology.get_decl(&pico_member_moniker).await.expect("piconet member had no decl");
        let profile_capability_name =
            CapabilityName(super::mock_profile_service_path(&member_spec));
        let mut expose_proto_decl = ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: profile_capability_name.clone(),
            target: ExposeTarget::Parent,
            target_name: profile_capability_name,
        };
        let expose_decl = ExposeDecl::Protocol(expose_proto_decl.clone());
        assert!(pico_member_decl.exposes.contains(&expose_decl));

        // root should have a similar-looking expose declaration for Profile, only the source
        // should be the child in question
        {
            expose_proto_decl.source = ExposeSource::Child(member_spec.name.to_string());
            let root_expose_decl = ExposeDecl::Protocol(expose_proto_decl);
            let root = topology.get_decl(&vec![].into()).await.expect("failed to get root");
            assert!(root.exposes.contains(&root_expose_decl));
        }
    }

    #[track_caller]
    async fn validate_mock_piconet_member<'a>(
        topology: &mut Realm,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // check that the piconet member exists
        let pico_member_moniker = vec![member_spec.name.to_string()].into();
        assert!(topology
            .contains(&pico_member_moniker)
            .await
            .expect("failed to check realm contents"));

        // Validate the `bredr.Profile` related routes.
        if member_spec.rfcomm_url.is_some() {
            validate_profile_routes_for_member_with_rfcomm(topology, member_spec).await;
        } else {
            validate_profile_routes_for_member(topology, member_spec).await;
        }

        // check that the piconet member has a use declaration for ProfileTest
        let pico_member_decl =
            topology.get_decl(&pico_member_moniker).await.expect("piconet member had no decl");
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: CapabilityName(bredr::ProfileTestMarker::PROTOCOL_NAME.to_string()),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: bredr::ProfileTestMarker::PROTOCOL_NAME.to_string(),
            },
            dependency_type: DependencyType::Strong,
        });
        assert!(pico_member_decl.uses.contains(&use_decl));

        // Check that the root offers ProfileTest to the piconet member from
        // the Mock Piconet Server
        let profile_test_name = CapabilityName(bredr::ProfileTestMarker::PROTOCOL_NAME.to_string());
        let root = topology.get_decl(&vec![].into()).await.expect("failed to get root");
        let offer_profile_test = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(super::mock_piconet_server_moniker().to_string()),
            source_name: profile_test_name.clone(),
            target: OfferTarget::Child(pico_member_moniker.to_string()),
            target_name: profile_test_name,
            dependency_type: DependencyType::Strong,
        });
        assert!(root.offers.contains(&offer_profile_test));

        // We don't check that the Mock Piconet Server exposes ProfileTest
        // because the builder won't actually know if this is true until it
        // resolves the component URL. We assume other tests validate the
        // Mock Piconet Server has this expose.
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_profile() {
        let mut test_harness = PiconetHarness::new().await;
        let profile_name = "test-profile-member";
        let profile_moniker: Moniker = vec![profile_name.to_string()].into();
        let interposer_name = super::interposer_name_for_profile(profile_name);

        // Add a profile with a fake URL
        let _profile_member = test_harness
            .add_profile(
                profile_name.to_string(),
                "fuchsia-pkg://fuchsia.com/example#meta/example.cm".to_string(),
            )
            .await
            .expect("failed to add profile");

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");
        assert!(topology.contains(&profile_moniker).await.expect("failed to check realm contents"));
        assert!(topology
            .contains(&vec![interposer_name.clone()].into())
            .await
            .expect("failed to check realm contents"));

        // validate routes

        // Profile is exposed by interposer
        let profile_capability_name =
            CapabilityName(bredr::ProfileMarker::PROTOCOL_NAME.to_string());
        let profile_expose = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: profile_capability_name.clone(),
            target: ExposeTarget::Parent,
            target_name: profile_capability_name.clone(),
        });
        let interposer = topology
            .get_decl(&vec![interposer_name.clone()].into())
            .await
            .expect("interposer not found!");
        interposer.exposes.contains(&profile_expose);

        // ProfileTest is used by interposer
        let profile_test_name = CapabilityName(bredr::ProfileTestMarker::PROTOCOL_NAME.to_string());
        let profile_test_use = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: profile_test_name.clone(),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: bredr::ProfileTestMarker::PROTOCOL_NAME.to_string(),
            },
            dependency_type: DependencyType::Strong,
        });
        assert!(interposer.uses.contains(&profile_test_use));

        // Profile is offered by root to profile from interposer
        let profile_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(interposer_name.clone()),
            source_name: profile_capability_name.clone(),
            target: OfferTarget::Child(profile_name.to_string()),
            target_name: profile_capability_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        let root = topology.get_decl(&vec![].into()).await.expect("unable to get root decl");
        assert!(root.offers.contains(&profile_offer));

        // ProfileTest is offered by root to interposer from Mock Piconet Server
        let profile_test_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(super::mock_piconet_server_moniker().to_string()),
            source_name: profile_test_name.clone(),
            target: OfferTarget::Child(interposer_name.clone()),
            target_name: profile_test_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        assert!(root.offers.contains(&profile_test_offer));

        // LogSink is offered by test root to interposer and profile.
        let log_capability_name = CapabilityName(LogSinkMarker::PROTOCOL_NAME.to_string());
        let log_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: log_capability_name.clone(),
            target: OfferTarget::Child(profile_name.to_string()),
            target_name: log_capability_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        assert!(root.offers.contains(&log_offer));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_profile_with_rfcomm() {
        let mut test_harness = PiconetHarness::new().await;

        let profile_name = "test-profile-member";
        let profile_moniker: Moniker = vec![profile_name.to_string()].into();
        let interposer_name = super::interposer_name_for_profile(profile_name);
        let bt_rfcomm_name = super::bt_rfcomm_moniker_for_member(profile_name);
        let profile_url = "fuchsia-pkg://fuchsia.com/example#meta/example.cm".to_string();
        let rfcomm_url = "fuchsia-pkg://fuchsia.com/example#meta/bt-rfcomm.cm".to_string();

        // Add a profile with a fake URL
        let _profile_member = test_harness
            .add_profile_with_capabilities(
                profile_name.to_string(),
                profile_url,
                Some(rfcomm_url),
                vec![],
                vec![],
            )
            .await
            .expect("failed to add profile");

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");
        assert!(topology.contains(&profile_moniker).await.expect("failed to check realm contents"));
        assert!(topology
            .contains(&vec![interposer_name.clone()].into())
            .await
            .expect("failed to check realm contents"));
        assert!(topology
            .contains(&vec![bt_rfcomm_name.clone()].into())
            .await
            .expect("failed to check realm contents"));

        // validate routes
        let profile_capability_name =
            CapabilityName(bredr::ProfileMarker::PROTOCOL_NAME.to_string());

        // `Profile` is offered by root to bt-rfcomm from interposer.
        let profile_offer1 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(interposer_name.clone()),
            source_name: profile_capability_name.clone(),
            target: OfferTarget::Child(bt_rfcomm_name.clone()),
            target_name: profile_capability_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        // `Profile` is offered from bt-rfcomm to profile.
        let profile_offer2 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(bt_rfcomm_name.clone()),
            source_name: profile_capability_name.clone(),
            target: OfferTarget::Child(profile_name.to_string()),
            target_name: profile_capability_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        let root = topology.get_decl(&vec![].into()).await.expect("unable to get root decl");
        assert!(root.offers.contains(&profile_offer1));
        assert!(root.offers.contains(&profile_offer2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_profile_with_additional_capabilities() {
        let mut test_harness = PiconetHarness::new().await;
        let profile_name = "test-profile-member";
        let profile_moniker: Moniker = vec![profile_name.to_string()].into();

        // Add a profile with a fake URL and some fake use & expose capabilities.
        let fake_cap1 = "Foo".to_string();
        let fake_cap2 = "Bar".to_string();
        let expose_capabilities =
            vec![Capability::protocol(fake_cap1.clone()), Capability::protocol(fake_cap2.clone())];
        let fake_cap3 = "Cat".to_string();
        let use_capabilities = vec![Capability::protocol(fake_cap3.clone())];
        let _profile_member = test_harness
            .add_profile_with_capabilities(
                profile_name.to_string(),
                "fuchsia-pkg://fuchsia.com/example#meta/example.cm".to_string(),
                None,
                use_capabilities,
                expose_capabilities,
            )
            .await
            .expect("failed to add profile");

        let mut topology = test_harness.update_routes_and_build().await.expect("should build");
        assert!(topology.contains(&profile_moniker).await.expect("failed to check realm contents"));

        // Validate the additional capability routes. See `test_add_profile` for validation
        // of Profile, ProfileTest, and LogSink routes.

        // `Foo` is exposed by the profile to parent.
        let fake_capability_name1 = CapabilityName(fake_cap1);
        let fake_capability_expose1 = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child(profile_name.to_string()),
            source_name: fake_capability_name1.clone(),
            target: ExposeTarget::Parent,
            target_name: fake_capability_name1.clone(),
        });
        // `Bar` is exposed by the profile to parent.
        let fake_capability_name2 = CapabilityName(fake_cap2);
        let fake_capability_expose2 = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child(profile_name.to_string()),
            source_name: fake_capability_name2.clone(),
            target: ExposeTarget::Parent,
            target_name: fake_capability_name2.clone(),
        });
        // `Cat` is used by the profile and exposed from above the test root.
        let fake_capability_name3 = CapabilityName(fake_cap3);
        let fake_capability_offer3 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: fake_capability_name3.clone(),
            target: OfferTarget::Child(profile_name.to_string()),
            target_name: fake_capability_name3,
            dependency_type: DependencyType::Strong,
        });

        let root = topology.get_decl(&vec![].into()).await.expect("unable to get root decl");
        assert!(root.exposes.contains(&fake_capability_expose1));
        assert!(root.exposes.contains(&fake_capability_expose2));
        assert!(root.offers.contains(&fake_capability_offer3));
    }
}
