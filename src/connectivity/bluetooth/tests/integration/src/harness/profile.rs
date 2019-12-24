// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::{self as bredr, ProfileEvent, ProfileMarker, ProfileProxy},
    fuchsia_bluetooth::expectation::asynchronous::{ExpectableState, ExpectationHarness},
    fuchsia_zircon as zx,
    futures::future::BoxFuture,
    futures::{FutureExt, StreamExt},
    std::sync::Arc,
};

use crate::harness::{control::ActivatedFakeHost, TestHarness};

impl TestHarness for ProfileHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-profile-0").await?;
            let proxy = fuchsia_component::client::connect_to_service::<ProfileMarker>()
                .context("Failed to connect to Profile serivce")?;
            let harness = ProfileHarness::new(proxy);

            let run_profile = handle_profile_events(harness.clone()).boxed();
            Ok((harness, fake_host, run_profile))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
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

    while let Some(evt) = events.next().await {
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
