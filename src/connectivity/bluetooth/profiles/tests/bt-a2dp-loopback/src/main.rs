// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, argh::FromArgs, bt_profile_test_server_client::ProfileTestHarness,
    fidl_fuchsia_bluetooth_bredr::LaunchInfo, fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId, fuchsia_zircon as zx, futures::future, log::info,
    matches::assert_matches, std::convert::TryInto,
};

/// A2DP component URL.
const A2DP_URL: &str = fuchsia_component::fuchsia_single_component_package_url!("bt-a2dp");

/// Defines the options available from the command line
#[derive(FromArgs)]
#[argh(description = "Bluetooth A2DP Loopback Test")]
struct Opt {
    #[argh(option, default = "10")]
    /// length in seconds to run for. Pass 0 to wait indefinitely.
    duration: usize,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opts: Opt = argh::from_env();
    fuchsia_syslog::init_with_tags(&["a2dp-loopback"])?;
    info!("Starting a2dp-loopback");

    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    info!("Create mock peer #1");
    // Create MockPeer #1 for source.
    let id1 = PeerId(1);
    let mock_peer1 = test_harness.register_peer(id1).await?;
    let launch_info = LaunchInfo { component_url: Some(A2DP_URL.to_string()), ..LaunchInfo::EMPTY };
    assert_matches!(mock_peer1.launch_profile(launch_info).await, Ok(()));

    info!("Create mock peer #2");
    // Create MockPeer #2 for sink, but disable the auto connect so they don't collide
    let id2 = PeerId(8);
    let mock_peer2 = test_harness.register_peer(id2).await?;
    let launch_info = LaunchInfo {
        component_url: Some(A2DP_URL.to_string()),
        arguments: Some(vec!["--initiator-delay".to_string(), "0".to_string()]),
        ..LaunchInfo::EMPTY
    };
    assert_matches!(mock_peer2.launch_profile(launch_info).await, Ok(()));

    if opts.duration > 0 {
        info!("Streaming for duration {}", opts.duration);
        fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(
            opts.duration.try_into()?,
        )))
        .await;
    } else {
        info!("Streaming forever...");
        future::pending::<()>().await;
    }
    Ok(())
}
