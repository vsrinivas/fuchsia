// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::peer_observer_timeout,
    anyhow::{format_err, Context, Error},
    fidl::{
        encoding::Decodable,
        endpoints::{self as f_end, DiscoverableService},
    },
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_logger::LogSinkMarker,
    fuchsia_async::{self as fasync, futures::TryStreamExt, DurationExt, TimeoutExt},
    fuchsia_bluetooth::types as bt_types,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        mock, Moniker, RealmInstance,
    },
    fuchsia_zircon as zx,
    futures::{stream::StreamExt, TryFutureExt},
};

static PROFILE_TEST_SERVER_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/bt-profile-test-server#meta/bt-profile-test-server-v2.cm";
static PROFILE_INTERPOSER_PREFIX: &str = "profile-interposer";

/// Specification data for creating a peer in the mock piconet. This may be a
/// peer that will be driven by test code or one that is an actual bluetooth
/// profile implementation.
pub struct PiconetMemberSpec {
    pub name: String,
    pub id: bt_types::PeerId,
    observer: Option<bredr::PeerObserverProxy>,
}

impl PiconetMemberSpec {
    pub fn get_profile_proxy(
        &self,
        topology: &RealmInstance,
    ) -> Result<bredr::ProfileProxy, anyhow::Error> {
        let (client, server) = f_end::create_endpoints::<bredr::ProfileMarker>()?;
        topology.root.connect_request_to_named_service_at_exposed_dir(
            &mock_profile_service_path(self),
            server.into_channel(),
        )?;
        client.into_proxy().map_err(|e| e.into())
    }

    /// Create a PiconetMemberSpec configured to be used with a Profile
    /// component which is under test.
    pub fn for_profile(name: String) -> (Self, bredr::PeerObserverRequestStream) {
        let (peer_proxy, peer_stream) =
            f_end::create_proxy_and_stream::<bredr::PeerObserverMarker>().unwrap();

        (Self { name, id: bt_types::PeerId::random(), observer: Some(peer_proxy) }, peer_stream)
    }

    /// Create a PiconetMemberSpec designed to be used with a peer that will be driven
    /// by test code.
    pub fn for_mock_peer(name: String) -> Self {
        Self { name, id: bt_types::PeerId::random(), observer: None }
    }
}

fn mock_profile_service_path(mock: &PiconetMemberSpec) -> String {
    format!("{}{}", bredr::ProfileMarker::SERVICE_NAME, mock.id)
}

pub struct PiconetMember {
    profile_svc: bredr::ProfileProxy,
}

impl PiconetMember {
    pub fn new(profile_service: bredr::ProfileProxy) -> Self {
        Self { profile_svc: profile_service }
    }

    pub fn new_from_spec(
        mock: PiconetMemberSpec,
        realm: &RealmInstance,
    ) -> Result<Self, anyhow::Error> {
        Ok(Self {
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
    pub profile_id: bt_types::PeerId,
}

impl ProfileObserver {
    pub fn new(stream: bredr::PeerObserverRequestStream, id: bt_types::PeerId) -> Self {
        Self { observer_stream: stream, profile_id: id }
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

/// Adds a profile. This also creates a component that sits between the profile
/// and the Profile Test Server. This node acts as a facade between the profile
/// under test and the Test Server. If the PiconetMemberSpec passed in contains
/// a Channel then PeerObserver events will be forwarded to that channel.
async fn add_profile<'a>(
    builder: &mut RealmBuilder,
    spec: &'a mut PiconetMemberSpec,
    server_moniker: String,
    profile_url: String,
) -> Result<(), Error> {
    let mock_piconet_member_name = interposer_name_for_profile(&spec.name);
    add_mock_piconet_component(
        builder,
        mock_piconet_member_name.clone(),
        spec.id,
        bredr::ProfileMarker::SERVICE_NAME.to_string(),
        spec.observer.take(),
    )
    .await?;

    // set up the profile
    {
        let _ = builder
            .add_eager_component(spec.name.to_string(), ComponentSource::Url(profile_url))
            .await?;
    }

    // Route:
    //   * Profile from mock piconet member to profile under test
    //   * ProfileTest from Profile Test Server to mock piconet member
    //   * LogSink from parent to the profile under test + mock piconet member.
    {
        builder.add_protocol_route::<bredr::ProfileMarker>(
            RouteEndpoint::component(mock_piconet_member_name.clone()),
            vec![RouteEndpoint::component(spec.name.to_string())],
        )?;

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
    }
    Ok(())
}

async fn add_mock_piconet_members(
    builder: &mut RealmBuilder,
    mocks: &'_ mut Vec<PiconetMemberSpec>,
    server_moniker: String,
) -> Result<(), Error> {
    for mock in mocks.iter_mut() {
        add_mock_piconet_member(builder, mock, server_moniker.clone())
            .await
            .context(format!("failed to add {}", mock.name))?;
    }
    Ok(())
}

async fn add_mock_piconet_member<'a, 'b>(
    builder: &mut RealmBuilder,
    mock: &'a mut PiconetMemberSpec,
    server_moniker: String,
) -> Result<(), Error> {
    add_mock_piconet_component(
        builder,
        mock.name.to_string(),
        mock.id,
        mock_profile_service_path(mock),
        mock.observer.take(),
    )
    .await?;

    let _ = builder.add_protocol_route::<bredr::ProfileTestMarker>(
        RouteEndpoint::component(&server_moniker),
        vec![RouteEndpoint::component(mock.name.clone())],
    )?;

    // This routes the fuchsia.bluetooth.bredr.Profile protocol from each mock
    // to above the root under a unique name like
    // fuchsia.bluetooth.bredr.Profile-3 where "3" is the peer ID of the mock.
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol(mock_profile_service_path(mock)),
        source: RouteEndpoint::component(mock.name.to_string()),
        targets: vec![RouteEndpoint::AboveRoot],
    })?;
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
/// Profile service, attaches it to the Profile Test Server, and wires up the
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

async fn add_profile_test_server(builder: &mut RealmBuilder) -> String {
    let name = test_profile_server_moniker().to_string();

    builder
        .add_component(name.clone(), ComponentSource::url(PROFILE_TEST_SERVER_URL_V2))
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

pub fn test_profile_server_moniker() -> Moniker {
    vec!["test-profile-server".to_string()].into()
}

pub fn interposer_name_for_profile(profile_name: &'_ str) -> String {
    format!("{}-{}", PROFILE_INTERPOSER_PREFIX, profile_name)
}

pub struct ProfileTestHarnessV2 {
    pub builder: RealmBuilder,
    pub ps_moniker: String,
}

impl ProfileTestHarnessV2 {
    pub async fn new() -> Self {
        let mut builder = RealmBuilder::new().await.expect("Couldn't create realm builder");
        let ps_moniker = add_profile_test_server(&mut builder).await;
        ProfileTestHarnessV2 { builder, ps_moniker }
    }

    pub async fn add_mock_piconet_members(
        &mut self,
        mocks: &'_ mut Vec<PiconetMemberSpec>,
    ) -> Result<(), Error> {
        add_mock_piconet_members(&mut self.builder, mocks, self.ps_moniker.clone()).await
    }

    pub async fn add_mock_piconet_member(
        &mut self,
        name: String,
    ) -> Result<PiconetMemberSpec, Error> {
        let mut mock = PiconetMemberSpec::for_mock_peer(name);

        self.add_mock_piconet_member_from_spec(&mut mock).await?;
        Ok(mock)
    }

    pub async fn add_mock_piconet_member_from_spec(
        &mut self,
        mock: &'_ mut PiconetMemberSpec,
    ) -> Result<(), Error> {
        add_mock_piconet_member(&mut self.builder, mock, self.ps_moniker.clone()).await
    }

    pub async fn build(self) -> Result<RealmInstance, Error> {
        self.builder.build().create().await.map_err(|e| e.into())
    }

    pub async fn add_profile(
        &mut self,
        name: String,
        profile_url: String,
    ) -> Result<ProfileObserver, Error> {
        let (mut spec, request_stream) = PiconetMemberSpec::for_profile(name);
        self.add_profile_from_spec(&mut spec, profile_url).await?;
        Ok(ProfileObserver::new(request_stream, spec.id))
    }

    pub async fn add_profile_from_spec(
        &mut self,
        spec: &mut PiconetMemberSpec,
        profile_url: String,
    ) -> Result<(), Error> {
        add_profile(&mut self.builder, spec, self.ps_moniker.clone(), profile_url).await
    }
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
        let test_harness = ProfileTestHarnessV2::new().await;
        let mut topology = test_harness.builder.build();
        let pts = topology.contains(&super::test_profile_server_moniker());
        assert!(pts);
        topology.create().await.expect("build failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_piconet_member() {
        let mut test_harness = ProfileTestHarnessV2::new().await;
        let member_name = "test-piconet-member";
        let member_spec = test_harness
            .add_mock_piconet_member(member_name.to_string())
            .await
            .expect("failed to add piconet member");
        assert_eq!(member_spec.name, member_name);

        let mut topology = test_harness.builder.build();
        validate_mock_piconet_member(&mut topology, &member_spec).await;
        let _profile_test_offer = topology.create().await.expect("build failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_multiple_piconet_members() {
        let mut test_harness = ProfileTestHarnessV2::new().await;
        let member1_name = "test-piconet-member".to_string();
        let member2_name = "test-piconet-member-two".to_string();
        let mut members = vec![
            PiconetMemberSpec::for_mock_peer(member1_name),
            PiconetMemberSpec::for_mock_peer(member2_name),
        ];

        test_harness
            .add_mock_piconet_members(&mut members)
            .await
            .expect("failed to add piconet members");

        let mut topology = test_harness.builder.build();

        for member in &members {
            validate_mock_piconet_member(&mut topology, member).await;
        }
        let _profile_test_offer = topology.create().await.expect("build failed");
    }

    async fn validate_mock_piconet_member<'a>(
        topology: &mut Realm,
        member_spec: &'a PiconetMemberSpec,
    ) {
        // check that the piconet member exists
        let pico_member_moniker = vec![member_spec.name.to_string()].into();
        assert!(topology.contains(&pico_member_moniker));

        // check that the mock piconet member has an expose declaration for the profile protocol
        let pico_member_decl =
            topology.get_decl_mut(&pico_member_moniker).expect("piconet member had no decl");
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

        // check that the piconet member has a use declaration for ProfileTest
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: CapabilityName(bredr::ProfileTestMarker::SERVICE_NAME.to_string()),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: bredr::ProfileTestMarker::SERVICE_NAME.to_string(),
            },
        });
        assert!(pico_member_decl.uses.contains(&use_decl));

        // root should have a similar-looking expose declaration for Profile,
        // only the source should be the child in question
        {
            expose_proto_decl.source = ExposeSource::Child(member_spec.name.to_string());
            let root_expose_decl = ExposeDecl::Protocol(expose_proto_decl);
            let root = topology.get_decl_mut(&vec![].into()).expect("failed to get root");
            assert!(root.exposes.contains(&root_expose_decl));
        }

        // Check that the root offers ProfileTest to the piconet member from
        // the Profile Test Server
        let profile_test_name = CapabilityName(bredr::ProfileTestMarker::SERVICE_NAME.to_string());
        let root = topology.get_decl_mut(&vec![].into()).expect("failed to get root");
        let offer_profile_test = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(super::test_profile_server_moniker().to_string()),
            source_name: profile_test_name.clone(),
            target: OfferTarget::Child(pico_member_moniker.to_string()),
            target_name: profile_test_name,
            dependency_type: DependencyType::Strong,
        });
        assert!(root.offers.contains(&offer_profile_test));

        // We don't check that the Profile Test Server exposes ProfileTest
        // because the builder won't actually know if this is true until it
        // resolves the component URL. We assume other tests validate the
        // Profile Test Server has this expose.
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_profile() {
        let mut test_harness = ProfileTestHarnessV2::new().await;
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

        let mut topology = test_harness.builder.build();
        assert!(topology.contains(&profile_moniker));
        assert!(topology.contains(&vec![interposer_name.clone()].into()));

        // validate routes

        // Profile is exposed by interposer
        let profile_capability_name =
            CapabilityName(bredr::ProfileMarker::SERVICE_NAME.to_string());
        let profile_expose = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: profile_capability_name.clone(),
            target: ExposeTarget::Parent,
            target_name: profile_capability_name.clone(),
        });
        let interposer = topology
            .get_decl_mut(&vec![interposer_name.clone()].into())
            .expect("interposer not found!");
        interposer.exposes.contains(&profile_expose);

        // ProfileTest is used by interposer
        let profile_test_name = CapabilityName(bredr::ProfileTestMarker::SERVICE_NAME.to_string());
        let profile_test_use = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: profile_test_name.clone(),
            target_path: CapabilityPath {
                dirname: "/svc".to_string(),
                basename: bredr::ProfileTestMarker::SERVICE_NAME.to_string(),
            },
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
        let root = topology.get_decl_mut(&vec![].into()).expect("unable to get root decl");
        assert!(root.offers.contains(&profile_offer));

        // ProfileTest is offered by root to interposer from Profile Test Server
        let profile_test_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Child(super::test_profile_server_moniker().to_string()),
            source_name: profile_test_name.clone(),
            target: OfferTarget::Child(interposer_name.clone()),
            target_name: profile_test_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        assert!(root.offers.contains(&profile_test_offer));

        // LogSink is offered by test root to interposer and profile.
        let log_capability_name = CapabilityName(LogSinkMarker::SERVICE_NAME.to_string());
        let log_offer = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: log_capability_name.clone(),
            target: OfferTarget::Child(profile_name.to_string()),
            target_name: log_capability_name.clone(),
            dependency_type: DependencyType::Strong,
        });
        assert!(root.offers.contains(&log_offer));
    }
}
