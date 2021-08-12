// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    at_commands::{self as at, SerDe},
    bitflags::bitflags,
    bt_manifest_integration_lib::{mock_component, mock_dev},
    fidl::{encoding::Decodable, endpoints::DiscoverableProtocolMarker},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::HfpMarker,
    fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_media::{AudioDeviceEnumeratorMarker, AudioDeviceEnumeratorRequest},
    fixture::fixture,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_audio_dai::test::mock_dai_dev_with_io_devices,
    fuchsia_bluetooth::types::{Channel, PeerId, Uuid},
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        mock::{Mock, MockHandles},
        RealmInstance,
    },
    fuchsia_zircon::Duration,
    futures::{channel::mpsc, stream::StreamExt, TryFutureExt},
    mock_piconet_client::v2::{BtProfileComponent, PiconetHarness, PiconetMember},
    std::convert::TryInto,
    test_call_manager::TestCallManager,
};

/// HFP-AG component V2 URL.
const HFP_AG_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/bt-hfp-audio-gateway-integration-tests#meta/bt-hfp-audio-gateway.cm";
/// RFCOMM component v2 URL.
const RFCOMM_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/bt-hfp-audio-gateway-integration-tests#meta/bt-rfcomm.cm";

/// The moniker for the Hands Free Profile Audio Gateway component under test.
const HFP_AG_MONIKER: &str = "bt-hfp-ag-profile";
/// The moniker for the mock dev/ directory provider.
const MOCK_DEV_MONIKER: &str = "mock-dev";
/// The moniker for a mock piconet member.
const MOCK_PICONET_MEMBER_MONIKER: &str = "mock-peer";

/// Timeout for data received over a Channel.
///
/// This time is expected to be:
///   a) sufficient to avoid flakes due to infra or resource contention
///   b) short enough to still provide useful feedback in those cases where asynchronous operations
///      fail
///   c) short enough to fail before the overall infra-imposed test timeout (currently 5 minutes)
const CHANNEL_TIMEOUT: Duration = Duration::from_seconds(2 * 60);

/// SDP Attribute ID for the Supported Features of HFP.
/// Defined in Assigned Numbers for SDP
/// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_ID_HFP_SUPPORTED_FEATURES: u16 = 0x0311;

bitflags! {
    /// Defined in HFP v1.8, Table 5.2
    struct HandsFreeFeaturesSdpAttribute: u16 {
        const ECHO_CANCELATION_AND_NOISE_REDUCTION = 0b0000_0001;
        const THREE_WAY_CALLING                    = 0b0000_0010;
        const CLI_PRESENTATION                     = 0b0000_0100;
        const VOICE_RECOGNITION                    = 0b0000_1000;
        const REMOTE_VOLUME_CONTROL                = 0b0001_0000;
        const WIDEBAND_SPEECH                      = 0b0010_0000;
        const ENHANCED_VOICE_RECOGNITION           = 0b0100_0000;
        const ENHANCED_VOICE_RECOGNITION_TEXT      = 0b1000_0000;
    }
}

/// Make the SDP definition for an HFP Hands Free service.
fn hfp_hf_service_definition() -> bredr::ServiceDefinition {
    let supported_features = HandsFreeFeaturesSdpAttribute::empty().bits();

    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![
            Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16).into(),
            Uuid::new16(bredr::ServiceClassProfileIdentifier::GenericAudio as u16).into(),
        ]),
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
            profile_id: bredr::ServiceClassProfileIdentifier::Handsfree,
            major_version: 1,
            minor_version: 8,
        }]),
        additional_attributes: Some(vec![bredr::Attribute {
            id: ATTR_ID_HFP_SUPPORTED_FEATURES,
            element: bredr::DataElement::Uint16(supported_features),
        }]),
        ..bredr::ServiceDefinition::new_empty()
    }
}

struct HfpAgIntegrationTest {
    /// The collection of components used for the integration test. Must be kept alive.
    test_realm: RealmInstance,
    /// The observer for the HFP component - used to assert on behavior.
    hfp_under_test: BtProfileComponent,
    test_driven_peer: PiconetMember,
    test_call_manager: TestCallManager,
}

impl HfpAgIntegrationTest {
    /// Builds the test topology used by the integration test.
    async fn setup_piconet_with_hfp_and_mock_peer() -> Self {
        let mut test_harness = PiconetHarness::new().await;

        // Mock piconet member with an RFCOMM intermediary.
        let spec = test_harness
            .add_mock_piconet_member(
                MOCK_PICONET_MEMBER_MONIKER.to_string(),
                Some(RFCOMM_URL_V2.to_string()),
            )
            .await
            .expect("failed to add mock piconet member");

        // The HFP profile under test.
        // - Add a fake `dev/` directory with DAI devices to be used by HFP.
        // - Add a provider for the `AudioDeviceEnumerator` capability used by HFP.
        // - Add the HFP component with a hermetic RFCOMM intermediary component. Expose the
        //   `fuchsia.bluetooth.hfp.Hfp` capability for the `test_call_manager`.
        add_mock_dai_devices(&mut test_harness.builder).await;
        add_mock_audio_device_enumerator_provider(&mut test_harness.builder).await;
        let hfp_under_test = test_harness
            .add_profile_with_capabilities(
                HFP_AG_MONIKER.to_string(),
                HFP_AG_URL_V2.to_string(),
                Some(RFCOMM_URL_V2.to_string()),
                vec![],
                vec![Capability::protocol(HfpMarker::PROTOCOL_NAME)],
            )
            .await
            .expect("failed to add HFP profile");

        let test_realm = test_harness.build().await.unwrap();

        // To be driven by the test.
        let test_driven_peer = PiconetMember::new_from_spec(spec, &test_realm)
            .expect("failed to get piconet member from spec");
        // Set up the test call manager to interact with the `Hfp` protocol.
        let test_call_manager = setup_test_call_manager(&test_realm, &hfp_under_test).await;

        Self { test_realm, hfp_under_test, test_driven_peer, test_call_manager }
    }
}

/// Adds a mock component to the `builder` that provides the `AudioDeviceEnumerator` capability.
/// Note: This implementation consumes but does not handle the FIDL requests. This is sufficient
/// for the current integration tests. We should consider injecting a v2 component that implements
/// this capability once it has been migrated to v2.
async fn add_mock_audio_device_enumerator_provider(builder: &mut RealmBuilder) {
    const MOCK_AUDIO_DEVICE_MONIKER: &str = "mock-audio-device-provider";
    let (sender, mut receiver) = mpsc::channel(0);
    fasync::Task::spawn(async move {
        while let Some(req) = receiver.next().await {
            log::info!("Received AudioDeviceEnumerator request: {:?}", req);
        }
    })
    .detach();

    let _ = builder
        .add_eager_component(
            MOCK_AUDIO_DEVICE_MONIKER,
            ComponentSource::Mock(Mock::new({
                let sender = sender.clone();
                move |mock_handles: MockHandles| {
                    Box::pin(mock_component::<
                        AudioDeviceEnumeratorMarker,
                        AudioDeviceEnumeratorRequest,
                    >(sender.clone(), mock_handles))
                }
            })),
        )
        .await
        .expect("Failed adding AudioDevice mock to topology");

    let _ = builder
        .add_protocol_route::<AudioDeviceEnumeratorMarker>(
            RouteEndpoint::component(MOCK_AUDIO_DEVICE_MONIKER),
            vec![RouteEndpoint::component(HFP_AG_MONIKER)],
        )
        .expect("Failed adding route for AudioDeviceEnumerator capability");
}

/// Adds a mock dev/ directory provider for the DAI devices used by HFP.
/// Routes the directory capability from the provider to the HFP component.
async fn add_mock_dai_devices(builder: &mut RealmBuilder) {
    let _ = builder
        .add_eager_component(
            MOCK_DEV_MONIKER,
            ComponentSource::Mock(Mock::new({
                move |mock_handles: MockHandles| {
                    Box::pin(mock_dev(
                        mock_handles,
                        mock_dai_dev_with_io_devices("input1".to_string(), "output1".to_string()),
                    ))
                }
            })),
        )
        .await
        .expect("Failed adding mock /dev provider to topology");

    let _ = builder
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-dai", "/dev/class/dai", fio2::RW_STAR_DIR),
            source: RouteEndpoint::component(MOCK_DEV_MONIKER),
            targets: vec![RouteEndpoint::component(HFP_AG_MONIKER)],
        })
        .expect("Failed adding route for DAI device directory");
}

/// Returns a TestCallManager that uses the `Hfp` protocol provided by the `hfp_component` in the
/// test `topology`.
async fn setup_test_call_manager(
    topology: &RealmInstance,
    hfp_component: &BtProfileComponent,
) -> TestCallManager {
    let hfp_protocol = hfp_component
        .connect_to_protocol::<HfpMarker>(topology)
        .expect("`Hfp` protocol should be available");
    let facade = TestCallManager::new();
    facade.register_manager(hfp_protocol).await.unwrap();
    facade
}

async fn setup_piconet<F, Fut>(_name: &str, test: F)
where
    F: FnOnce(HfpAgIntegrationTest) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    let test_fixture = HfpAgIntegrationTest::setup_piconet_with_hfp_and_mock_peer().await;
    test(test_fixture).await
}

/// Tests that HFP correctly advertises it's services and can be
/// discovered by another peer in the mock piconet.
#[fixture(setup_piconet)]
#[fuchsia::test]
async fn test_hfp_ag_service_advertisement(tf: HfpAgIntegrationTest) {
    let HfpAgIntegrationTest {
        test_realm: _tr,
        hfp_under_test,
        test_driven_peer,
        test_call_manager: _tcm,
    } = tf;

    let mut results_requests = test_driven_peer
        .register_service_search(
            bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway,
            vec![],
        )
        .expect("can register service search");

    // Expect the test-driven piconet member to discover the HFP component.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await.expect("should receive search results");
    assert_eq!(hfp_under_test.peer_id(), peer_id.into());
    responder.send().unwrap();
}

/// Expects a connection request on the `connect_requests` stream from the `other` peer.
/// Returns the Channel.
#[track_caller]
async fn expect_connection(
    connect_requests: &mut bredr::ConnectionReceiverRequestStream,
    other: PeerId,
) -> Channel {
    match connect_requests.select_next_some().await.expect("should receive connection request") {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(other, peer_id.into());
            channel.try_into().unwrap()
        }
    }
}

/// Tests that HFP correctly searches for Handsfree, discovers a peer providing it, and attempts to
/// connect to the peer.
#[fixture(setup_piconet)]
#[fuchsia::test]
async fn test_hfp_search_and_connect(tf: HfpAgIntegrationTest) {
    let HfpAgIntegrationTest {
        test_realm: _tr,
        mut hfp_under_test,
        test_driven_peer,
        test_call_manager: _tcm,
    } = tf;

    // Remote peer advertises an HFP HF service.
    let service_defs = vec![hfp_hf_service_definition()];
    let mut connect_requests = test_driven_peer
        .register_service_advertisement(service_defs)
        .expect("can register service search");

    // We expect HFP to discover Peer #1's service advertisement.
    hfp_under_test
        .expect_observer_service_found_request(test_driven_peer.peer_id())
        .await
        .expect("should observe search results");

    // We then expect HFP to attempt to connect to Peer #1.
    let _channel = expect_connection(&mut connect_requests, hfp_under_test.peer_id()).await;
}

/// Expects data on the provided `channel` and verifies the contents with the `expected` AT
/// messages.
#[track_caller]
async fn expect_data(channel: &mut Channel, expected: Vec<at::Response>) {
    let expected_bytes: Vec<u8> = expected
        .into_iter()
        .map(|exp| {
            let exps = vec![exp];
            let mut bytes = Vec::new();
            at::Response::serialize(&mut bytes, &exps).expect("serialization should succeed");
            bytes
        })
        .flatten()
        .collect();

    loop {
        let mut actual_bytes = Vec::new();
        let read_result = channel
            .read_datagram(&mut actual_bytes)
            .on_timeout(CHANNEL_TIMEOUT.after_now(), move || Err(fidl::Status::TIMED_OUT))
            .await
            .expect("reading from channel is ok");
        if read_result != 0 {
            assert_eq!(actual_bytes, expected_bytes);
            break;
        }
    }
}

/// Serializes and sends the provided AT `command` using the `channel` and then
/// expects the `expected` response.
#[track_caller]
async fn send_command_and_expect_response(
    channel: &mut Channel,
    command: at::Command,
    expected: Vec<at::Response>,
) {
    // Serialize and send.
    let mut bytes = Vec::new();
    let mut commands = vec![command];
    at::Command::serialize(&mut bytes, &mut commands).expect("serialization should succeed");
    let _ = channel.as_ref().write(&bytes);

    // Expect the `expected` data as a response.
    expect_data(channel, expected).await;
}

// TODO(fxb/71668) Stop using raw bytes.
const CIND_TEST_RESPONSE_BYTES: &[u8] = b"+CIND: \
(\"service\",(0,1)),\
(\"call\",(0,1)),\
(\"callsetup\",(0,3)),\
(\"callheld\",(0,2)),\
(\"signal\",(0,5)),\
(\"roam\",(0,1)),\
(\"battchg\",(0,5)\
)";

/// Tests that HFP correctly responds to the SLC Initialization procedure after the
/// RFCOMM channel has been connected.
/// Note: This integration test validates that the expected responses are received, but
/// does not validate individual field values (e.g the exact features or exact indicators) as
/// this is implementation specific.
#[fixture(setup_piconet)]
#[fuchsia::test]
async fn test_hfp_full_slc_init_procedure(tf: HfpAgIntegrationTest) {
    let HfpAgIntegrationTest {
        test_realm: _tr,
        mut hfp_under_test,
        test_driven_peer,
        test_call_manager: _tcm,
    } = tf;

    // Peer #1 advertises an HFP HF service.
    let service_defs = vec![hfp_hf_service_definition()];
    let mut connect_requests = test_driven_peer
        .register_service_advertisement(service_defs)
        .expect("should register advertisement");

    // We expect HFP to discover Peer #1's service advertisement.
    hfp_under_test
        .expect_observer_service_found_request(test_driven_peer.peer_id())
        .await
        .expect("should observe search results");

    // We then expect HFP to attempt to connect to Peer #1.
    let mut remote = expect_connection(&mut connect_requests, hfp_under_test.peer_id()).await;

    // Peer sends its HF features to the HFP component (AG) - we expect HFP to send
    // its AG features back.
    // TODO: We shouldn't need to assert on the specific features in the response.
    let hf_features_cmd = at::Command::Brsf { features: 0b11_1111_1111_1111 };
    send_command_and_expect_response(
        &mut remote,
        hf_features_cmd,
        vec![at::success(at::Success::Brsf { features: 3907i64 }), at::Response::Ok],
    )
    .await;

    // Peer sends its supported codecs - expect OK back.
    let peer_supported_codecs_cmd = at::Command::Bac { codecs: vec![] };
    send_command_and_expect_response(
        &mut remote,
        peer_supported_codecs_cmd,
        vec![at::Response::Ok],
    )
    .await;

    let indicator_test_cmd = at::Command::CindTest {};
    let expected3 = at::Response::RawBytes(Vec::from(CIND_TEST_RESPONSE_BYTES));
    send_command_and_expect_response(
        &mut remote,
        indicator_test_cmd,
        vec![expected3, at::Response::Ok],
    )
    .await;

    let indicator_read_cmd = at::Command::CindRead {};
    let expected4 = at::success(at::Success::Cind {
        service: false,
        call: false,
        callsetup: 0,
        callheld: 0,
        roam: false,
        signal: 0,
        battchg: 5,
    });
    send_command_and_expect_response(
        &mut remote,
        indicator_read_cmd,
        vec![expected4, at::Response::Ok],
    )
    .await;

    // Peer enables indicator reporting (ind = 1).
    let peer_enable_ind_reporting_cmd = at::Command::Cmer { mode: 3, keyp: 0, disp: 0, ind: 1 };
    send_command_and_expect_response(
        &mut remote,
        peer_enable_ind_reporting_cmd,
        vec![at::Response::Ok],
    )
    .await;

    let call_hold_info_cmd = at::Command::ChldTest {};
    let expected6 = at::success(at::Success::Chld {
        commands: vec![
            "0".to_string(),
            "1".to_string(),
            "1X".to_string(),
            "2".to_string(),
            "2X".to_string(),
        ],
    });
    send_command_and_expect_response(
        &mut remote,
        call_hold_info_cmd,
        vec![expected6, at::Response::Ok],
    )
    .await;

    let peer_supported_indicators_cmd =
        at::Command::Bind { indicators: vec![at::BluetoothHFIndicator::BatteryLevel as i64] };
    send_command_and_expect_response(
        &mut remote,
        peer_supported_indicators_cmd,
        vec![at::Response::Ok],
    )
    .await;

    let peer_request_indicators_cmd = at::Command::BindTest {};
    let expected8 = at::success(at::Success::BindList {
        indicators: vec![
            at::BluetoothHFIndicator::EnhancedSafety,
            at::BluetoothHFIndicator::BatteryLevel,
        ],
    });
    send_command_and_expect_response(
        &mut remote,
        peer_request_indicators_cmd,
        vec![expected8, at::Response::Ok],
    )
    .await;

    let peer_request_ind_status_cmd = at::Command::BindRead {};

    let expected_safety_off = at::success(at::Success::BindStatus {
        anum: at::BluetoothHFIndicator::EnhancedSafety,
        state: false,
    });
    let expected_battery_level_on = at::success(at::Success::BindStatus {
        anum: at::BluetoothHFIndicator::BatteryLevel,
        state: true,
    });
    send_command_and_expect_response(
        &mut remote,
        peer_request_ind_status_cmd,
        vec![expected_safety_off, expected_battery_level_on, at::Response::Ok],
    )
    .await;
}
