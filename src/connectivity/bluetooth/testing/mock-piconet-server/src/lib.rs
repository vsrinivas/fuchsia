// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use cm_rust::{ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget};
use fidl::encoding::Decodable;
use fidl::endpoints::{self as f_end, DiscoverableProtocolMarker};
use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_component_test as ftest;
use fidl_fuchsia_logger::LogSinkMarker;
use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
use fuchsia_bluetooth::types as bt_types;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use fuchsia_zircon::{self as zx, Duration, DurationNum};
use futures::{stream::StreamExt, TryFutureExt, TryStreamExt};
use tracing::info;

/// Timeout for updates over the PeerObserver of a MockPeer.
///
/// This time is expected to be:
///   a) sufficient to avoid flakes due to infra or resource contention
///   b) short enough to still provide useful feedback in those cases where asynchronous operations
///      fail
///   c) short enough to fail before the overall infra-imposed test timeout (currently 5 minutes)
const TIMEOUT_SECONDS: i64 = 2 * 60;

pub fn peer_observer_timeout() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

static MOCK_PICONET_SERVER_URL: &str = "#meta/mock-piconet-server.cm";
static PROFILE_INTERPOSER_PREFIX: &str = "profile-interposer";
static BT_RFCOMM_PREFIX: &str = "bt-rfcomm";

/// Returns the protocol name from the `capability` or an Error if it cannot be retrieved.
fn protocol_name_from_capability(capability: &ftest::Capability) -> Result<String, Error> {
    if let ftest::Capability::Protocol(ftest::Protocol { name: Some(name), .. }) = capability {
        Ok(name.clone())
    } else {
        Err(format_err!("Not a protocol capability: {:?}", capability))
    }
}

fn expose_decl(name: &str, id: bt_types::PeerId, capability_name: &str) -> ExposeDecl {
    ExposeDecl::Protocol(ExposeProtocolDecl {
        source: ExposeSource::Child(name.to_string()),
        source_name: capability_name.into(),
        target: ExposeTarget::Parent,
        target_name: capability_path_for_peer_id(id, capability_name).into(),
    })
}

/// Specification data for creating a peer in the mock piconet. This may be a
/// peer that will be driven by test code or one that is an actual Bluetooth
/// profile implementation.
#[derive(Clone, Debug)]
pub struct PiconetMemberSpec {
    pub name: String,
    pub id: bt_types::PeerId,
    /// The optional hermetic RFCOMM component URL to be used by piconet members
    /// that need RFCOMM functionality.
    rfcomm_url: Option<String>,
    /// Expose declarations for additional capabilities provided by this piconet member. This is
    /// typically empty for test driven peers (unless an RFCOMM intermediary is specified), and may
    /// be populated for specs describing an actual Bluetooth profile implementation.
    /// The exposed capabilities will be available at a unique path associated with this spec (e.g
    /// `fuchsia.bluetooth.hfp.Hfp-321abc` where `321abc` is the PeerId for this member).
    /// Note: Only protocol capabilities may be specified here.
    expose_decls: Vec<ExposeDecl>,
    observer: Option<bredr::PeerObserverProxy>,
}

impl PiconetMemberSpec {
    pub fn get_profile_proxy(
        &self,
        topology: &RealmInstance,
    ) -> Result<bredr::ProfileProxy, anyhow::Error> {
        info!("Received request to get `bredr.Profile` for piconet member: {:?}", self.id);
        let (client, server) = f_end::create_proxy::<bredr::ProfileMarker>()?;
        topology.root.connect_request_to_named_protocol_at_exposed_dir(
            &capability_path_for_mock::<bredr::ProfileMarker>(self),
            server.into_channel(),
        )?;
        Ok(client)
    }

    /// Create a PiconetMemberSpec configured to be used with a Profile
    /// component which is under test.
    /// `rfcomm_url` is the URL for an optional v2 RFCOMM component that will sit between the
    /// Profile and the Mock Piconet Server.
    /// `expose_capabilities` specifies protocol capabilities provided by this Profile component to
    /// be exposed above the test root.
    pub fn for_profile(
        name: String,
        rfcomm_url: Option<String>,
        expose_capabilities: Vec<ftest::Capability>,
    ) -> Result<(Self, bredr::PeerObserverRequestStream), Error> {
        let id = bt_types::PeerId::random();
        let capability_names = expose_capabilities
            .iter()
            .map(protocol_name_from_capability)
            .collect::<Result<Vec<_>, _>>()?;
        let expose_decls = capability_names
            .iter()
            .map(|capability_name| expose_decl(&name, id, capability_name))
            .collect();
        let (peer_proxy, peer_stream) =
            f_end::create_proxy_and_stream::<bredr::PeerObserverMarker>().unwrap();

        Ok((Self { name, id, rfcomm_url, expose_decls, observer: Some(peer_proxy) }, peer_stream))
    }

    /// Create a PiconetMemberSpec designed to be used with a peer that will be driven
    /// by test code.
    /// `rfcomm_url` is the URL for an optional v2 RFCOMM component that will sit between the
    /// mock peer and the integration test client.
    pub fn for_mock_peer(name: String, rfcomm_url: Option<String>) -> Self {
        let id = bt_types::PeerId::random();
        // If the RFCOMM URL is specified, then we expect to expose the `bredr.Profile` capability
        // above the test root at a unique path.
        let expose_decls = if rfcomm_url.is_some() {
            let rfcomm_name = bt_rfcomm_moniker_for_member(&name);
            vec![expose_decl(&rfcomm_name, id, bredr::ProfileMarker::PROTOCOL_NAME)]
        } else {
            Vec::new()
        };
        Self { name, id, rfcomm_url, expose_decls, observer: None }
    }
}

fn capability_path_for_mock<S: DiscoverableProtocolMarker>(mock: &PiconetMemberSpec) -> String {
    capability_path_for_peer_id(mock.id, S::PROTOCOL_NAME)
}

fn capability_path_for_peer_id(id: bt_types::PeerId, capability_name: &str) -> String {
    format!("{}-{}", capability_name, id)
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

/// Represents a Bluetooth profile-under-test in the test topology.
///
/// Provides helpers designed to observe what the real profile implementation is doing.
/// Provides access to any capabilities that have been exposed by this profile.
/// Note: Only capabilities that are specified in the `expose_capabilities` field of the
///       PiconetHarness::add_profile_with_capabilities() method will be available for connection.
pub struct BtProfileComponent {
    observer_stream: bredr::PeerObserverRequestStream,
    profile_id: bt_types::PeerId,
}

impl BtProfileComponent {
    pub fn new(stream: bredr::PeerObserverRequestStream, id: bt_types::PeerId) -> Self {
        Self { observer_stream: stream, profile_id: id }
    }

    pub fn peer_id(&self) -> bt_types::PeerId {
        self.profile_id
    }

    /// Connects to the protocol `S` provided by this Profile. Returns the client end on success,
    /// Error if the capability is not available.
    pub fn connect_to_protocol<S: DiscoverableProtocolMarker>(
        &self,
        topology: &RealmInstance,
    ) -> Result<S::Proxy, Error> {
        let (client, server) = f_end::create_proxy::<S>()?;
        topology.root.connect_request_to_named_protocol_at_exposed_dir(
            &capability_path_for_peer_id(self.profile_id, S::PROTOCOL_NAME),
            server.into_channel(),
        )?;
        Ok(client)
    }

    /// Expects a request over the `PeerObserver` protocol for this MockPeer.
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
/// `additional_routes` specifies capability routings for any protocols used/exposed by the
/// profile.
async fn add_profile_to_topology<'a>(
    builder: &RealmBuilder,
    spec: &'a mut PiconetMemberSpec,
    server_moniker: String,
    profile_url: String,
    additional_routes: Vec<Route>,
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
            .add_child(spec.name.to_string(), profile_url, ChildOptions::new().eager())
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
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<bredr::ProfileMarker>())
                        .from(Ref::child(&mock_piconet_member_name))
                        .to(Ref::child(&rfcomm_moniker)),
                )
                .await?;
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<bredr::ProfileMarker>())
                        .from(Ref::child(&rfcomm_moniker))
                        .to(Ref::child(&spec.name)),
                )
                .await?;
        } else {
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol::<bredr::ProfileMarker>())
                        .from(Ref::child(&mock_piconet_member_name))
                        .to(Ref::child(&spec.name)),
                )
                .await?;
        }

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<bredr::ProfileTestMarker>())
                    .from(Ref::child(&server_moniker))
                    .to(Ref::child(&mock_piconet_member_name)),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<LogSinkMarker>())
                    .from(Ref::parent())
                    .to(Ref::child(&spec.name))
                    .to(Ref::child(&mock_piconet_member_name)),
            )
            .await?;

        for route in additional_routes {
            let _ = builder.add_route(route).await?;
        }
    }
    Ok(())
}

async fn add_mock_piconet_member<'a, 'b>(
    builder: &RealmBuilder,
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
        capability_path_for_mock::<bredr::ProfileMarker>(mock)
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
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<bredr::ProfileTestMarker>())
                    .from(Ref::child(&server_moniker))
                    .to(Ref::child(&mock.name)),
            )
            .await?;

        if mock.rfcomm_url.is_some() {
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(profile_path))
                        .from(Ref::child(&mock.name))
                        .to(Ref::child(&rfcomm_moniker)),
                )
                .await?;
        } else {
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(profile_path))
                        .from(Ref::child(&mock.name))
                        .to(Ref::parent()),
                )
                .await?;
        }
    }

    Ok(())
}

/// Add the mock piconet member to the Realm. If observer_src is None a channel
/// is created and the server end shipped off to a future to be drained.
async fn add_mock_piconet_component(
    builder: &RealmBuilder,
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
        .add_local_child(
            name,
            move |m: LocalComponentHandles| {
                let observer = observer.clone();
                Box::pin(piconet_member(m, id, profile_svc_path.clone(), observer))
            },
            ChildOptions::new(),
        )
        .await
        .map(|_| ())
        .map_err(|e| e.into())
}

/// Drives the mock piconet member. This receives the open request for the
/// Profile service, attaches it to the Mock Piconet Server, and wires up the
/// the PeerObserver.
async fn piconet_member(
    handles: LocalComponentHandles,
    id: bt_types::PeerId,
    profile_svc_path: String,
    peer_observer: bredr::PeerObserverProxy,
) -> Result<(), Error> {
    // connect to the profile service to drive the mock peer
    let pro_test = handles.connect_to_protocol::<bredr::ProfileTestMarker>()?;
    let mut fs = ServiceFs::new();

    let _ = fs.dir("svc").add_service_at(profile_svc_path, move |chan: zx::Channel| {
        info!("Received ServiceFs `Profile` connection request for piconet_member: {:?}", id);
        let profile_test = pro_test.clone();
        let observer = peer_observer.clone();

        fasync::Task::local(async move {
            let (client, observer_req_stream) =
                register_piconet_member(&profile_test, id).await.unwrap();

            let err_str = format!("Couldn't connect to `Profile` for peer {:?}", id);

            let _ = client.connect_proxy_(chan.into()).await.expect(&err_str);

            // keep us running and hold on until termination to keep the mock alive
            fwd_observer_callbacks(observer_req_stream, &observer, id).await.unwrap();
        })
        .detach();
        Some(())
    });

    let _ = fs.serve_connection(handles.outgoing_dir).expect("failed to serve service fs");
    fs.collect::<()>().await;

    Ok(())
}

/// Use the ProfileTestProxy to register a piconet member with the Bluetooth Profile Test
/// Server.
async fn register_piconet_member(
    profile_test_proxy: &bredr::ProfileTestProxy,
    id: bt_types::PeerId,
) -> Result<(bredr::MockPeerProxy, bredr::PeerObserverRequestStream), Error> {
    info!("Sending RegisterPeer request for peer {:?} to the Mock Piconet Server.", id);
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
        }
    }
    Ok(())
}

async fn add_mock_piconet_server(builder: &RealmBuilder) -> String {
    let name = mock_piconet_server_moniker().to_string();

    let mock_piconet_server = builder
        .add_child(name.clone(), MOCK_PICONET_SERVER_URL, ChildOptions::new())
        .await
        .expect("failed to add");

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<LogSinkMarker>())
                .from(Ref::parent())
                .to(&mock_piconet_server),
        )
        .await
        .unwrap();
    name
}

/// Adds the `bt-rfcomm` component, identified by the `url`, to the component topology.
async fn add_bt_rfcomm_intermediary(
    builder: &RealmBuilder,
    moniker: String,
    url: String,
) -> Result<(), Error> {
    let bt_rfcomm = builder.add_child(moniker.clone(), url, ChildOptions::new().eager()).await?;

    let _ = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<LogSinkMarker>())
                .from(Ref::parent())
                .to(&bt_rfcomm),
        )
        .await?;
    Ok(())
}

fn mock_piconet_server_moniker() -> String {
    "mock-piconet-server".to_string()
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
    profiles: Vec<PiconetMemberSpec>,
    piconet_members: Vec<PiconetMemberSpec>,
}

impl PiconetHarness {
    pub async fn new() -> Self {
        let builder = RealmBuilder::new().await.expect("Couldn't create realm builder");
        let ps_moniker = add_mock_piconet_server(&builder).await;
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
        add_mock_piconet_member(&self.builder, mock, self.ps_moniker.clone()).await?;
        self.piconet_members.push(mock.clone());
        Ok(())
    }

    /// Updates expose routes specified by the profiles and piconet members.
    async fn update_routes(&self) -> Result<(), Error> {
        info!(
            "Building test realm with profiles: {:?} and piconet members: {:?}",
            self.profiles, self.piconet_members
        );
        let mut root_decl = self.builder.get_realm_decl().await.expect("failed to get root");

        let mut piconet_member_exposes =
            self.piconet_members.iter().map(|spec| spec.expose_decls.clone()).flatten().collect();
        let mut profile_member_exposes =
            self.profiles.iter().map(|spec| spec.expose_decls.clone()).flatten().collect();
        root_decl.exposes.append(&mut piconet_member_exposes);
        root_decl.exposes.append(&mut profile_member_exposes);

        // Update the root decl with the modified `expose` routes.
        self.builder.replace_realm_decl(root_decl).await.expect("Should be able to set root decl");
        Ok(())
    }

    pub async fn build(self) -> Result<RealmInstance, Error> {
        self.update_routes().await?;
        self.builder.build().await.map_err(|e| e.into())
    }

    /// Add a profile with moniker `name` to the test topology. The profile should be
    /// accessible via the provided `profile_url` and will be launched during the test.
    ///
    /// Returns an observer for the launched profile.
    pub async fn add_profile(
        &mut self,
        name: String,
        profile_url: String,
    ) -> Result<BtProfileComponent, Error> {
        self.add_profile_with_capabilities(name, profile_url, None, vec![], vec![]).await
    }

    /// Add a profile with moniker `name` to the test topology.
    ///
    /// `profile_url` specifies the component URL of the profile under test.
    /// `rfcomm_url` specifies the optional hermetic RFCOMM component URL to be used as an
    /// intermediary in the test topology.
    /// `use_capabilities` specifies any capabilities used by the profile that will be
    /// provided outside the test realm.
    /// `expose_capabilities` specifies any protocol capabilities provided by the profile to be
    /// available in the outgoing directory of the test realm root.
    ///
    /// Returns an observer for the launched profile.
    pub async fn add_profile_with_capabilities(
        &mut self,
        name: String,
        profile_url: String,
        rfcomm_url: Option<String>,
        use_capabilities: Vec<ftest::Capability>,
        expose_capabilities: Vec<ftest::Capability>,
    ) -> Result<BtProfileComponent, Error> {
        let (mut spec, request_stream) =
            PiconetMemberSpec::for_profile(name, rfcomm_url, expose_capabilities.clone())?;
        // Use capabilities can be directly turned into routes.
        let route = route_from_capabilities(
            use_capabilities,
            Ref::parent(),
            vec![Ref::child(spec.name.clone())],
        );

        self.add_profile_from_spec(&mut spec, profile_url, vec![route]).await?;
        Ok(BtProfileComponent::new(request_stream, spec.id))
    }

    async fn add_profile_from_spec(
        &mut self,
        spec: &mut PiconetMemberSpec,
        profile_url: String,
        capabilities: Vec<Route>,
    ) -> Result<(), Error> {
        add_profile_to_topology(
            &self.builder,
            spec,
            self.ps_moniker.clone(),
            profile_url,
            capabilities,
        )
        .await?;
        self.profiles.push(spec.clone());
        Ok(())
    }
}

/// Builds a set of capability routes from `capabilities` that will be routed from
/// `source` to the `targets`.
pub fn route_from_capabilities(
    capabilities: Vec<ftest::Capability>,
    source: Ref,
    targets: Vec<Ref>,
) -> Route {
    let mut route = Route::new().from(source);
    for capability in capabilities {
        route = route.capability(capability);
    }
    for target in targets {
        route = route.to(target);
    }
    route
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        cm_rust::{
            Availability, CapabilityName, CapabilityPath, DependencyType, ExposeDecl,
            ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferDecl, OfferProtocolDecl,
            OfferSource, OfferTarget, UseDecl, UseProtocolDecl, UseSource,
        },
        fidl_fuchsia_component_test as fctest,
        fuchsia_component_test::error::Error as RealmBuilderError,
    };

    async fn assert_realm_contains(builder: &RealmBuilder, child_name: &str) {
        let err = builder
            .add_child(child_name, "test://example-url", ChildOptions::new())
            .await
            .expect_err("failed to check realm contents");
        assert_matches!(
            err,
            RealmBuilderError::ServerError(fctest::RealmBuilderError::ChildAlreadyExists)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_profile_server_added() {
        let test_harness = PiconetHarness::new().await;
        test_harness.update_routes().await.expect("should update routes");
        assert_realm_contains(&test_harness.builder, &super::mock_piconet_server_moniker()).await;
        let _ = test_harness.builder.build().await.expect("build failed");
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

        test_harness.update_routes().await.expect("should update routes");
        validate_mock_piconet_member(&test_harness.builder, &member_spec).await;
        let _profile_test_offer = test_harness.builder.build().await.expect("build failed");
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

        test_harness.update_routes().await.expect("should update routes");
        validate_mock_piconet_member(&test_harness.builder, &member_spec).await;

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

        test_harness.update_routes().await.expect("should update routes");

        for member in &members {
            validate_mock_piconet_member(&test_harness.builder, member).await;
        }
        let _profile_test_offer = test_harness.builder.build().await.expect("build failed");
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

        test_harness.update_routes().await.expect("should update routes");

        for member in &members {
            validate_mock_piconet_member(&test_harness.builder, member).await;
        }

        // Note: We don't `create()` the test realm because the `rfcomm_url` does not exist which
        // will cause component resolving to fail.
    }

    #[track_caller]
    async fn validate_profile_routes_for_member_with_rfcomm<'a>(
        builder: &RealmBuilder,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // Piconet member should have an expose declaration of Profile.
        let profile_capability_name = bredr::ProfileMarker::PROTOCOL_NAME.to_string();
        let pico_member_decl = builder
            .get_component_decl(member_spec.name.clone())
            .await
            .expect("piconet member had no decl");
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
            let custom_profile_capability_name = CapabilityName(super::capability_path_for_mock::<
                bredr::ProfileMarker,
            >(&member_spec));
            let custom_expose_profile_decl = ExposeProtocolDecl {
                source: ExposeSource::Child(bt_rfcomm_name),
                source_name: CapabilityName(profile_capability_name.to_string()),
                target: ExposeTarget::Parent,
                target_name: CapabilityName(custom_profile_capability_name.to_string()),
            };
            let root_expose_decl = ExposeDecl::Protocol(custom_expose_profile_decl);
            let root = builder.get_realm_decl().await.expect("failed to get root");
            assert!(root.exposes.contains(&root_expose_decl));
        }
    }

    #[track_caller]
    async fn validate_profile_routes_for_member<'a>(
        builder: &RealmBuilder,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // Check that the mock piconet member has an expose declaration for the profile protocol
        let pico_member_decl = builder
            .get_component_decl(member_spec.name.clone())
            .await
            .expect("piconet member had no decl");
        let profile_capability_name =
            CapabilityName(super::capability_path_for_mock::<bredr::ProfileMarker>(&member_spec));
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
            let root = builder.get_realm_decl().await.expect("failed to get root");
            assert!(root.exposes.contains(&root_expose_decl));
        }
    }

    #[track_caller]
    async fn validate_mock_piconet_member<'a>(
        builder: &RealmBuilder,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // check that the piconet member exists
        assert_realm_contains(builder, &member_spec.name).await;

        // Validate the `bredr.Profile` related routes.
        if member_spec.rfcomm_url.is_some() {
            validate_profile_routes_for_member_with_rfcomm(builder, member_spec).await;
        } else {
            validate_profile_routes_for_member(builder, member_spec).await;
        }

        // check that the piconet member has a use declaration for ProfileTest
        let pico_member_decl = builder
            .get_component_decl(member_spec.name.clone())
            .await
            .expect("piconet member had no decl");
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: CapabilityName(bredr::ProfileTestMarker::PROTOCOL_NAME.to_string()),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: bredr::ProfileTestMarker::PROTOCOL_NAME.to_string(),
            },
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        assert!(pico_member_decl.uses.contains(&use_decl));

        // Check that the root offers ProfileTest to the piconet member from
        // the Mock Piconet Server
        let profile_test_name = CapabilityName(bredr::ProfileTestMarker::PROTOCOL_NAME.to_string());
        let root = builder.get_realm_decl().await.expect("failed to get root");
        let offer_profile_test = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::static_child(super::mock_piconet_server_moniker().to_string()),
            source_name: profile_test_name.clone(),
            target: OfferTarget::static_child(member_spec.name.clone()),
            target_name: profile_test_name,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
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
        let interposer_name = super::interposer_name_for_profile(profile_name);

        // Add a profile with a fake URL
        let _profile_member = test_harness
            .add_profile(
                profile_name.to_string(),
                "fuchsia-pkg://fuchsia.com/example#meta/example.cm".to_string(),
            )
            .await
            .expect("failed to add profile");

        test_harness.update_routes().await.expect("should update routes");
        assert_realm_contains(&test_harness.builder, &profile_name).await;
        assert_realm_contains(&test_harness.builder, &interposer_name).await;

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
        let interposer = test_harness
            .builder
            .get_component_decl(interposer_name.clone())
            .await
            .expect("interposer not found!");
        assert!(interposer.exposes.contains(&profile_expose));

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
            availability: Availability::Required,
        });
        assert!(interposer.uses.contains(&profile_test_use));

        // Profile is offered by root to profile from interposer
        let profile_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::static_child(interposer_name.clone()),
            source_name: profile_capability_name.clone(),
            target: OfferTarget::static_child(profile_name.to_string()),
            target_name: profile_capability_name.clone(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let root = test_harness.builder.get_realm_decl().await.expect("unable to get root decl");
        assert!(root.offers.contains(&profile_offer));

        // ProfileTest is offered by root to interposer from Mock Piconet Server
        let profile_test_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::static_child(super::mock_piconet_server_moniker().to_string()),
            source_name: profile_test_name.clone(),
            target: OfferTarget::static_child(interposer_name.clone()),
            target_name: profile_test_name.clone(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        assert!(root.offers.contains(&profile_test_offer));

        // LogSink is offered by test root to interposer and profile.
        let log_capability_name = CapabilityName(LogSinkMarker::PROTOCOL_NAME.to_string());
        let log_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: log_capability_name.clone(),
            target: OfferTarget::static_child(profile_name.to_string()),
            target_name: log_capability_name.clone(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        assert!(root.offers.contains(&log_offer));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_profile_with_rfcomm() {
        let mut test_harness = PiconetHarness::new().await;

        let profile_name = "test-profile-member";
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

        test_harness.update_routes().await.expect("should update routes");
        assert_realm_contains(&test_harness.builder, &profile_name).await;
        assert_realm_contains(&test_harness.builder, &interposer_name).await;
        assert_realm_contains(&test_harness.builder, &bt_rfcomm_name).await;

        // validate routes
        let profile_capability_name =
            CapabilityName(bredr::ProfileMarker::PROTOCOL_NAME.to_string());

        // `Profile` is offered by root to bt-rfcomm from interposer.
        let profile_offer1 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::static_child(interposer_name.clone()),
            source_name: profile_capability_name.clone(),
            target: OfferTarget::static_child(bt_rfcomm_name.clone()),
            target_name: profile_capability_name.clone(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        // `Profile` is offered from bt-rfcomm to profile.
        let profile_offer2 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::static_child(bt_rfcomm_name.clone()),
            source_name: profile_capability_name.clone(),
            target: OfferTarget::static_child(profile_name.to_string()),
            target_name: profile_capability_name.clone(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });
        let root = test_harness.builder.get_realm_decl().await.expect("unable to get root decl");
        assert!(root.offers.contains(&profile_offer1));
        assert!(root.offers.contains(&profile_offer2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_profile_with_additional_capabilities() {
        let mut test_harness = PiconetHarness::new().await;
        let profile_name = "test-profile-member";

        // Add a profile with a fake URL and some fake use & expose capabilities.
        let fake_cap1 = "Foo".to_string();
        let fake_cap2 = "Bar".to_string();
        let expose_capabilities = vec![
            Capability::protocol_by_name(fake_cap1.clone()).into(),
            Capability::protocol_by_name(fake_cap2.clone()).into(),
        ];
        let fake_cap3 = "Cat".to_string();
        let use_capabilities = vec![Capability::protocol_by_name(fake_cap3.clone()).into()];
        let profile_member = test_harness
            .add_profile_with_capabilities(
                profile_name.to_string(),
                "fuchsia-pkg://fuchsia.com/example#meta/example.cm".to_string(),
                None,
                use_capabilities,
                expose_capabilities,
            )
            .await
            .expect("failed to add profile");

        test_harness.update_routes().await.expect("should update routes");
        assert_realm_contains(&test_harness.builder, &profile_name).await;

        // Validate the additional capability routes. See `test_add_profile` for validation
        // of Profile, ProfileTest, and LogSink routes.

        // `Foo` is exposed by the profile to parent.
        let fake_capability_expose1 = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child(profile_name.to_string()),
            source_name: fake_cap1.clone().into(),
            target: ExposeTarget::Parent,
            target_name: capability_path_for_peer_id(profile_member.peer_id(), &fake_cap1).into(),
        });
        // `Bar` is exposed by the profile to parent.
        let fake_capability_expose2 = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child(profile_name.to_string()),
            source_name: fake_cap2.clone().into(),
            target: ExposeTarget::Parent,
            target_name: capability_path_for_peer_id(profile_member.peer_id(), &fake_cap2).into(),
        });
        // `Cat` is used by the profile and exposed from above the test root.
        let fake_capability_offer3 = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: fake_cap3.clone().into(),
            target: OfferTarget::static_child(profile_name.to_string()),
            target_name: fake_cap3.into(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let root = test_harness.builder.get_realm_decl().await.expect("unable to get root decl");
        assert!(root.exposes.contains(&fake_capability_expose1));
        assert!(root.exposes.contains(&fake_capability_expose2));
        assert!(root.offers.contains(&fake_capability_offer3));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_profiles_with_same_expose_is_ok() {
        let mut test_harness = PiconetHarness::new().await;
        let profile_name1 = "test-profile-1";
        let profile_name2 = "test-profile-2";

        // Both profiles expose the same protocol capability.
        let fake_cap = "FooBarAmazing".to_string();
        let expose_capabilities = vec![Capability::protocol_by_name(fake_cap.clone()).into()];

        let profile_member1 = test_harness
            .add_profile_with_capabilities(
                profile_name1.to_string(),
                "fuchsia-pkg://fuchsia.com/example#meta/example-profile1.cm".to_string(),
                None,
                vec![],
                expose_capabilities.clone(),
            )
            .await
            .expect("failed to add profile1");
        let profile_member2 = test_harness
            .add_profile_with_capabilities(
                profile_name2.to_string(),
                "fuchsia-pkg://fuchsia.com/example#meta/example-profile2.cm".to_string(),
                None,
                vec![],
                expose_capabilities,
            )
            .await
            .expect("failed to add profile2");

        test_harness.update_routes().await.expect("should update routes");
        assert_realm_contains(&test_harness.builder, &profile_name1).await;
        assert_realm_contains(&test_harness.builder, &profile_name2).await;

        // Validate that `fake_cap` is exposed by both profiles, and is OK.
        let profile1_expose = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child(profile_name1.to_string()),
            source_name: fake_cap.clone().into(),
            target: ExposeTarget::Parent,
            target_name: capability_path_for_peer_id(profile_member1.peer_id(), &fake_cap).into(),
        });
        let profile2_expose = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child(profile_name2.to_string()),
            source_name: fake_cap.clone().into(),
            target: ExposeTarget::Parent,
            target_name: capability_path_for_peer_id(profile_member2.peer_id(), &fake_cap).into(),
        });

        let root = test_harness.builder.get_realm_decl().await.expect("unable to get root decl");
        assert!(root.exposes.contains(&profile1_expose));
        assert!(root.exposes.contains(&profile2_expose));
    }
}
