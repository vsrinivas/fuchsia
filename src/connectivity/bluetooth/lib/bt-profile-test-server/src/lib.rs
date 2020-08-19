// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::encoding::Decodable,
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_bluetooth_bredr::{
        ChannelParameters, ConnectionReceiverRequestStream, MockPeerMarker, MockPeerProxy,
        PeerObserverMarker, PeerObserverRequest, PeerObserverRequestStream, ProfileMarker,
        ProfileProxy, ProfileTestMarker, ProfileTestProxy, SearchResultsRequestStream,
        ServiceClassProfileIdentifier, ServiceDefinition,
    },
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::types::PeerId,
    fuchsia_component::{client, client::App, fuchsia_single_component_package_url},
    fuchsia_zircon::{Duration, DurationNum},
    futures::{stream::StreamExt, TryFutureExt},
};

/// The component URL of the Profile Test Server - used in integration tests.
static PROFILE_TEST_SERVER_URL: &str =
    "fuchsia-pkg://fuchsia.com/bt-profile-test-server#meta/bt-profile-test-server.cmx";

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

#[derive(Debug)]
/// The supported Bluetooth Profiles that can be launched.
pub enum Profile {
    AudioSink,
    AudioSource,
    Avrcp,
}

impl Profile {
    /// Converts the `Profile` enum to a fuchsia component URL, represented
    /// by a string.
    fn to_component_url(&self) -> String {
        match self {
            Profile::AudioSource => {
                fuchsia_single_component_package_url!("bt-a2dp-source").to_string()
            }
            Profile::AudioSink => fuchsia_single_component_package_url!("bt-a2dp-sink").to_string(),
            Profile::Avrcp => fuchsia_single_component_package_url!("bt-avrcp").to_string(),
        }
    }
}

/// The `ProfileTestHarness` provides functionality for writing integration tests
/// for our Rust profiles.
/// A client of `ProfileTestHarness` can import this object and write integration
/// tests using the common helpers provided in the Impl block.
/// All helpers leverage the ProfileTest API. See `fuchsia.bluetooth.bredr.ProfileTest`
/// for more documentation on the API.
pub struct ProfileTestHarness {
    /// A handle to the ProfileTest service. This is acquired by launching the Profile Test Server
    /// component, which provides the ProfileTest service.
    /// This can be used to register peers in the mock piconet.
    profile_test_svc: ProfileTestProxy,

    /// A handle to the launched Profile Test Server. This must be kept alive for the lifetime
    /// of the `ProfileTestHarness` object.
    _profile_test_server: App,
}

impl ProfileTestHarness {
    /// Creates a new `ProfileTestHarness` by launching the Profile Test Server and connecting to the
    /// ProfileTest service.
    pub fn new() -> Result<Self, Error> {
        let launcher = client::launcher().context("Failed to get the launcher")?;
        let app = client::launch(&launcher, PROFILE_TEST_SERVER_URL.to_string(), None)
            .context("failed to launch the profile test server")?;

        let profile_test_svc = app
            .connect_to_service::<ProfileTestMarker>()
            .context("Failed to connect to Bluetooth ProfileTest service")?;
        Ok(Self { profile_test_svc, _profile_test_server: app })
    }

    /// Registers a peer in the Profile Test Server database.
    /// `peer_id` is the unique identifier for the peer.
    ///
    /// Returns a MockPeer that can be used to control peer behavior.
    pub async fn register_peer(&self, peer_id: PeerId) -> Result<MockPeer, Error> {
        let (mock_client, mock_server) =
            create_proxy::<MockPeerMarker>().expect("couldn't create endpoints");
        let (update_client, update_server) =
            create_request_stream::<PeerObserverMarker>().expect("couldn't create endpoints");

        self.profile_test_svc
            .register_peer(&mut peer_id.into(), mock_server, update_client)
            .await?;

        MockPeer::new(mock_client, update_server).await
    }
}

/// A peer in the fake piconet.
/// Use the `MockPeer` object to manipulate peer behavior in the test.
///
/// Common usage: Use the `profile_svc` for direct peer control, OR
/// use `launch_profile()` to launch a profile that will drive peer behavior.
/// It does not make sense to use both in the context of a single integration test.
pub struct MockPeer {
    /// For controlling peer behavior.
    mock_peer: MockPeerProxy,

    /// For controlling peer behavior using the `Profile` service.
    profile_svc: ProfileProxy,

    /// Relays updates about this peer.
    observer: PeerObserverRequestStream,
}

impl MockPeer {
    async fn new(
        mock_peer: MockPeerProxy,
        observer: PeerObserverRequestStream,
    ) -> Result<Self, Error> {
        // Eagerly register a `ProfileProxy` for direct peer manipulation.
        let (profile_svc, profile_server) =
            create_proxy::<ProfileMarker>().expect("couldn't create endpoints");
        mock_peer.connect_proxy_(profile_server).await?;

        Ok(Self { mock_peer, profile_svc, observer })
    }

    /// Launches a `profile` for this mock peer.
    /// Once the profile is launched, the behavior of the mock peer is driven by the profile.
    ///
    /// Returns the result of launching the profile.
    pub async fn launch_profile(&self, profile: Profile) -> Result<bool, Error> {
        let url = profile.to_component_url();
        self.mock_peer.launch_profile(&url).await.map_err(|e| format_err!("{:?}", e))
    }

    /// Expects a request over the PeerObserver protocol for this MockPeer.
    ///
    /// Returns the request if successful.
    pub async fn expect_observer_request(&mut self) -> Result<PeerObserverRequest, Error> {
        // The Future is gated by a timeout so that tests consistently terminate.
        self.observer
            .select_next_some()
            .map_err(|e| format_err!("{:?}", e))
            .on_timeout(peer_observer_timeout().after_now(), move || {
                Err(format_err!("observer timed out"))
            })
            .await
    }

    /// Register a service search by a `profile_client` for services that match `svc_id`.
    ///
    /// Returns a stream of search results that can be polled to receive new requests.
    pub async fn register_service_search(
        &self,
        svc_id: ServiceClassProfileIdentifier,
        attributes: Vec<u16>,
    ) -> Result<SearchResultsRequestStream, Error> {
        let (results_client, results_requests) =
            create_request_stream().expect("couldn't create endpoints");
        self.profile_svc.search(svc_id, &attributes, results_client)?;
        Ok(results_requests)
    }

    /// Register a service advertisement for a `profile_client` with the provided
    /// `service_defs`.
    ///
    /// Returns a stream of connection requests that can be polled to receive new requests.
    pub async fn register_service_advertisement(
        &self,
        service_defs: Vec<ServiceDefinition>,
    ) -> Result<ConnectionReceiverRequestStream, Error> {
        let (connect_client, connect_requests) =
            create_request_stream().context("ConnectionReceiver creation")?;
        let _ = self.profile_svc.advertise(
            &mut service_defs.into_iter(),
            ChannelParameters::new_empty(),
            connect_client,
        );

        Ok(connect_requests)
    }
}
