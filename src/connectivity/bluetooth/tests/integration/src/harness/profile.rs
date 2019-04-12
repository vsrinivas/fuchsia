// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_bredr::{self as bredr, ProfileEvent, ProfileMarker, ProfileProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::expectation::asynchronous::{ExpectableState, ExpectationHarness},
    fuchsia_zircon as zx,
    futures::{Future, StreamExt, TryFutureExt},
    std::sync::Arc,
};

use crate::harness::{control::ActivatedFakeHost, TestHarness};

pub async fn run_profile_test_async<F, Fut>(test: F) -> Result<(), Error>
where
    F: FnOnce(ProfileHarness) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let fake_host = await!(ActivatedFakeHost::new("bt-hci-integration-profile-0"))?;

    let proxy = fuchsia_component::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Profile serivce")?;

    let state = ProfileHarness::new(proxy);

    fasync::spawn(
        handle_profile_events(state.clone())
            .unwrap_or_else(|e| eprintln!("Error handling profile events: {:?}", e)),
    );

    let result = await!(test(state));

    await!(fake_host.release())?;

    result
}

impl TestHarness for ProfileHarness {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(run_profile_test_async(test_func))
    }
}

#[derive(Clone)]
pub struct ConnectedChannel {
    pub device_id: String,
    pub service_id: u64,
    pub channel: Arc<zx::Socket>,
    pub protocol: Arc<bredr::ProtocolDescriptor>,
}

#[derive(Clone)]
pub struct ServiceFound {
    pub peer_id: String,
    pub profile: Arc<bredr::ProfileDescriptor>,
    pub attributes: Arc<Vec<bredr::Attribute>>,
}

/// A snapshot of the Profile State
#[derive(Default, Clone)]
pub struct ProfileState {
    pub connected_channels: Vec<ConnectedChannel>,
    pub services_found: Vec<ServiceFound>,
}

pub type ProfileHarness = ExpectationHarness<ProfileState, ProfileProxy>;

pub async fn handle_profile_events(harness: ProfileHarness) -> Result<(), Error> {
    let mut events = harness.aux().take_event_stream();

    while let Some(evt) = await!(events.next()) {
        match evt? {
            ProfileEvent::OnConnected { device_id, service_id, channel, protocol } => {
                harness.write_state().connected_channels.push(ConnectedChannel {
                    device_id,
                    service_id,
                    channel: Arc::new(channel),
                    protocol: Arc::new(protocol),
                });
                harness.notify_state_changed();
            }
            ProfileEvent::OnServiceFound { peer_id, profile, attributes } => {
                harness.write_state().services_found.push(ServiceFound {
                    peer_id,
                    profile: Arc::new(profile),
                    attributes: Arc::new(attributes),
                });
                harness.notify_state_changed();
            }
        };
    }
    Ok(())
}
