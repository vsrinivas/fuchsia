// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_control::{ControlMarker, ControlProxy},
    fuchsia_async::DurationExt,
    fuchsia_bluetooth::{
        constants::HOST_DEVICE_DIR,
        device_watcher::DeviceWatcher,
        expectation::{
            asynchronous::{ExpectableState, ExpectableStateExt, ExpectationHarness},
            Predicate,
        },
        hci_emulator::Emulator,
    },
    fuchsia_inspect_contrib::reader::{ArchiveReader, ComponentSelector, Inspect, NodeHierarchy},
    fuchsia_zircon::DurationNum,
    futures::{future::BoxFuture, FutureExt},
    std::path::PathBuf,
};

use crate::{harness::TestHarness, tests::timeout_duration};

const RETRY_TIMEOUT_SECONDS: i64 = 1;

pub async fn expect_hierarchies(
    harness: &InspectHarness,
    min_num: usize,
) -> Result<InspectState, Error> {
    harness
        .when_satisfied(
            Predicate::<InspectState>::predicate(
                move |state| state.hierarchies.len() >= min_num,
                "Expected number of hierarchies received",
            ),
            timeout_duration(),
        )
        .await
}

#[derive(Default, Clone)]
pub struct InspectState {
    pub moniker: Vec<String>,
    pub hierarchies: Vec<NodeHierarchy>,
}

pub type InspectHarness = ExpectationHarness<InspectState, ControlProxy>;

pub async fn handle_inspect_updates(harness: InspectHarness) -> Result<(), Error> {
    loop {
        if harness.read().moniker.len() > 0 {
            let fetcher = ArchiveReader::new()
                .add_selector(ComponentSelector::new(harness.read().moniker.clone()));
            harness.write_state().hierarchies = fetcher
                .snapshot::<Inspect>()
                .await?
                .into_iter()
                .flat_map(|result| result.payload)
                .collect();
            harness.notify_state_changed();
        }
        fuchsia_async::Timer::new(RETRY_TIMEOUT_SECONDS.seconds().after_now()).await;
    }
}

pub async fn new_inspect_harness() -> Result<(InspectHarness, Emulator, PathBuf), Error> {
    let emulator: Emulator = Emulator::create("bt-integration-test-host").await?;
    let host_dev = emulator.publish_and_wait_for_host(Emulator::default_settings()).await?;
    let host_path = host_dev.path().to_path_buf();

    let proxy = fuchsia_component::client::connect_to_service::<ControlMarker>()
        .context("Failed to connect to Control service")?;

    let inspect_harness = InspectHarness::new(proxy);
    Ok((inspect_harness, emulator, host_path))
}

impl TestHarness for InspectHarness {
    type Env = (PathBuf, Emulator);
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let (harness, emulator, host_path) = new_inspect_harness().await?;
            let run_inspect = handle_inspect_updates(harness.clone()).boxed();
            Ok((harness, (host_path, emulator), run_inspect))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        let (host_path, mut emulator) = env;
        async move {
            // Shut down the fake bt-hci device and make sure the bt-host device gets removed.
            let mut watcher = DeviceWatcher::new(HOST_DEVICE_DIR, timeout_duration()).await?;
            emulator.destroy_and_wait().await?;
            watcher.watch_removed(&host_path).await
        }
        .boxed()
    }
}
