// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    at_commands::{self as at, SerDe},
    bitflags::bitflags,
    bt_profile_test_server_client::{MockPeer, ProfileTestHarness},
    fidl::{
        encoding::Decodable,
        endpoints::{ServerEnd, ServiceMarker},
    },
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::HfpMarker,
    fidl_fuchsia_sys::EnvironmentOptions,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_bluetooth::types::{Channel, PeerId, Uuid},
    fuchsia_component::server::{NestedEnvironment, ServiceFs, ServiceObj},
    fuchsia_zircon::Duration,
    futures::{stream::StreamExt, TryFutureExt},
    matches::assert_matches,
    std::convert::TryInto,
    test_call_manager::{TestCallManager, HFP_AG_URL},
};

/// SDP Attribute ID for the Supported Features of HFP.
/// Defined in Assigned Numbers for SDP
/// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_ID_HFP_SUPPORTED_FEATURES: u16 = 0x0311;

/// Timeout for data received over a Channel.
///
/// This time is expected to be:
///   a) sufficient to avoid flakes due to infra or resource contention
///   b) short enough to still provide useful feedback in those cases where asynchronous operations
///      fail
///   c) short enough to fail before the overall infra-imposed test timeout (currently 5 minutes)
const CHANNEL_TIMEOUT: Duration = Duration::from_seconds(2 * 60);

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

fn hfp_launch_info() -> bredr::LaunchInfo {
    let url = Some(HFP_AG_URL.to_string());
    bredr::LaunchInfo { component_url: url.clone(), ..bredr::LaunchInfo::EMPTY }
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

/// Tests that HFP correctly advertises it's services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn test_hfp_ag_service_advertisement() {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // Create MockPeer #1 to be driven by the test.
    let id1 = PeerId(1);
    let remote_peer = test_harness.register_peer(id1).await.unwrap();

    // Peer #1 adds a search for HFP AG in the piconet.
    let mut results_requests = remote_peer
        .register_service_search(
            bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway,
            vec![],
        )
        .await
        .unwrap();

    // MockPeer #2 is the profile-under-test: HFP.
    let id2 = PeerId(2);
    let hfp_under_test = test_harness.register_peer(id2).await.unwrap();
    hfp_under_test.launch_profile(hfp_launch_info()).await.expect("launch profile should be ok");

    // We expect Peer #1 to discover HFP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await.unwrap();
    assert_eq!(id2, peer_id.into());
    responder.send().unwrap();
}

fn launch_hfp(
    url: &str,
) -> (ServiceFs<ServiceObj<'_, fidl::Channel>>, NestedEnvironment, fuchsia_component::client::App) {
    let mut fs = ServiceFs::new();
    fs.add_service_at(bredr::ProfileMarker::NAME, |chan| Some(chan));
    let options = EnvironmentOptions {
        inherit_parent_services: true,
        use_parent_runners: false,
        kill_on_oom: false,
        delete_storage_on_death: false,
    };
    let env = fs.create_salted_nested_environment_with_options(&"hfp", options).unwrap();
    let app = fuchsia_component::client::launch(env.launcher(), url.to_string(), None).unwrap();
    (fs, env, app)
}

async fn connect_profile_to_peer(
    peer: &mut MockPeer,
    fs: &mut ServiceFs<ServiceObj<'_, fidl::Channel>>,
) -> Result<(), Error> {
    let channel = fs.next().await.ok_or(format_err!("Connection expected from hfp component"))?;
    let server_end = ServerEnd::<bredr::ProfileMarker>::new(channel);
    peer.connect_proxy(server_end).await?;
    Ok(())
}

/// Expects a connection request on the `connect_requests` stream from the `other` peer.
/// Returns the Channel.
#[track_caller]
async fn expect_connection(
    connect_requests: &mut bredr::ConnectionReceiverRequestStream,
    other: PeerId,
) -> Channel {
    match connect_requests.select_next_some().await.unwrap() {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(other, peer_id.into());
            channel.try_into().unwrap()
        }
    }
}

/// Tests that HFP correctly searches for Handsfree, discovers a mock peer
/// providing it, and attempts to connect to the mock peer.
#[fasync::run_singlethreaded(test)]
async fn test_hfp_search_and_connect() {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(0x1111);
    let mut remote_peer = test_harness.register_peer(id1).await.unwrap();

    // Peer #1 advertises an HFP HF service.
    let service_defs = vec![hfp_hf_service_definition()];
    let mut connect_requests =
        remote_peer.register_service_advertisement(service_defs).await.unwrap();

    // MockPeer #2 is the profile-under-test: HFP.
    let id2 = PeerId(0x2222);
    let mut hfp_under_test = test_harness.register_peer(id2).await.unwrap();

    // Launch hfp component and wire it up to MockPeer #2
    let (mut fs, _env, app) = launch_hfp(HFP_AG_URL);
    connect_profile_to_peer(&mut hfp_under_test, &mut fs).await.unwrap();

    let proxy = app.connect_to_protocol::<HfpMarker>().unwrap();
    let facade = TestCallManager::new();
    facade.register_manager(proxy).await.unwrap();

    // We expect HFP to discover Peer #1's service advertisement.
    assert_matches!(hfp_under_test.expect_observer_service_found_request(id1).await, Ok(()));

    // We then expect HFP to attempt to connect to Peer #1.
    let _channel = expect_connection(&mut connect_requests, id2).await;

    // The observer of Peer #1 should be relayed of the connection attempt.
    assert_matches!(remote_peer.expect_observer_connection_request(id2).await, Ok(()));
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

    let mut actual_bytes = Vec::new();
    let read_result = channel
        .read_datagram(&mut actual_bytes)
        .on_timeout(CHANNEL_TIMEOUT.after_now(), move || Err(fidl::Status::TIMED_OUT))
        .await;
    assert_matches!(read_result, Ok(_));
    assert_eq!(actual_bytes, expected_bytes);
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
#[fasync::run_singlethreaded(test)]
async fn test_hfp_full_slc_init_procedure() {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(0x55);
    let mut remote_peer = test_harness.register_peer(id1).await.unwrap();

    // Peer #1 advertises an HFP HF service.
    let service_defs = vec![hfp_hf_service_definition()];
    let mut connect_requests =
        remote_peer.register_service_advertisement(service_defs).await.unwrap();

    // MockPeer #2 is the profile-under-test: HFP.
    let id2 = PeerId(0x66);
    let mut hfp_under_test = test_harness.register_peer(id2).await.unwrap();

    // Launch hfp component and wire it up to MockPeer #2
    let (mut fs, _env, app) = launch_hfp(HFP_AG_URL);
    connect_profile_to_peer(&mut hfp_under_test, &mut fs).await.unwrap();

    let proxy = app.connect_to_protocol::<HfpMarker>().unwrap();
    let facade = TestCallManager::new();
    facade.register_manager(proxy).await.unwrap();

    // We expect HFP to discover Peer #1's service advertisement.
    assert_matches!(hfp_under_test.expect_observer_service_found_request(id1).await, Ok(()));

    // We then expect HFP to attempt to connect to Peer #1.
    let mut remote = expect_connection(&mut connect_requests, id2).await;
    // The observer of Peer #1 should be relayed of the connection attempt.
    assert_matches!(remote_peer.expect_observer_connection_request(id2).await, Ok(()));

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
