// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_bredr::PeerObserverRequest;
use fidl_fuchsia_media::{AudioDeviceEnumeratorMarker, SessionAudioConsumerFactoryMarker};
use fidl_fuchsia_mediacodec::CodecFactoryMarker;
use fidl_fuchsia_metrics::MetricEventLoggerFactoryMarker;
use fidl_fuchsia_sysmem::AllocatorMarker;
use fidl_fuchsia_tracing_provider::RegistryMarker;
use fuchsia_async as fasync;
use fuchsia_component_test::{Capability, RealmInstance};
use fuchsia_zircon as zx;
use mock_piconet_client::{BtProfileComponent, PiconetHarness};
use tracing::info;

const A2DP_SOURCE_URL: &str = "#meta/bt-a2dp.cm";
const A2DP_SINK_URL: &str = "#meta/bt-a2dp-sink.cm";
const A2DP_SOURCE_MONIKER: &str = "a2dp-source";
const A2DP_SINK_MONIKER: &str = "a2dp-sink";

struct LoopbackIntegrationTest {
    test_realm: RealmInstance,
    a2dp_source: BtProfileComponent,
    a2dp_sink: BtProfileComponent,
}

async fn setup_piconet_with_two_a2dp_components() -> LoopbackIntegrationTest {
    let mut test_harness = PiconetHarness::new().await;

    let mut use_capabilities = vec![
        Capability::protocol::<CodecFactoryMarker>().into(),
        Capability::protocol::<MetricEventLoggerFactoryMarker>().into(),
        Capability::protocol::<AllocatorMarker>().into(),
        Capability::protocol::<SessionAudioConsumerFactoryMarker>().into(),
        Capability::protocol::<AudioDeviceEnumeratorMarker>().into(),
        Capability::protocol::<RegistryMarker>().into(),
    ];
    // Tracing must be enabled for source for some e2e tests
    let a2dp_source = test_harness
        .add_profile_with_capabilities(
            A2DP_SOURCE_MONIKER.to_string(),
            A2DP_SOURCE_URL.to_string(),
            None,
            use_capabilities.clone(),
            vec![],
        )
        .await
        .expect("can add a2dp source profile");

    // Remove the Tracing profile, to confirm component works without
    let _ = use_capabilities.pop();
    let a2dp_sink = test_harness
        .add_profile_with_capabilities(
            A2DP_SINK_MONIKER.to_string(),
            A2DP_SINK_URL.to_string(),
            None,
            use_capabilities,
            vec![],
        )
        .await
        .expect("can add a2dp sink profile");

    let test_realm = test_harness.build().await.expect("test topology should build");
    info!("Test topology built");

    LoopbackIntegrationTest { test_realm, a2dp_source, a2dp_sink }
}

#[fuchsia::test]
async fn main() {
    info!("Starting a2dp-loopback");

    let LoopbackIntegrationTest { test_realm: _tr, a2dp_source, mut a2dp_sink } =
        setup_piconet_with_two_a2dp_components().await;

    // Loop till peer connects
    loop {
        match a2dp_sink.expect_observer_request().await.unwrap() {
            PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
                info!("A2DP Source (Peer #1) connected to A2DP Sink (Peer #2)");
                assert_eq!(peer_id, a2dp_source.peer_id().into());
                let _ = responder.send().unwrap();
                break;
            }
            PeerObserverRequest::ServiceFound { peer_id, responder, .. } => {
                info!("A2DP Sink (Peer #2) discovered A2DP Source (Peer #1)");
                assert_eq!(peer_id, a2dp_source.peer_id().into());
                let _ = responder.send().unwrap();
            }
        }
    }

    // The test will stream for 10 seconds.
    // TODO(fxbug.dev/104010): Verify that audio packets are transferred from Source to Sink.
    const STREAMING_DURATION: i64 = 10;
    info!("Streaming for duration {}", STREAMING_DURATION);
    fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(STREAMING_DURATION))).await;
    info!("Finished streaming.")
}
