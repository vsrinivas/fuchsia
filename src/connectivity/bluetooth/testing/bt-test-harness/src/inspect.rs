// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    diagnostics_reader::{ArchiveReader, ComponentSelector, DiagnosticsHierarchy, Inspect},
    fidl_fuchsia_bluetooth_sys::{AccessMarker, AccessProxy},
    fuchsia_async::DurationExt,
    fuchsia_bluetooth::expectation::{
        asynchronous::{
            expectable, Expectable, ExpectableExt, ExpectableState, ExpectableStateExt,
        },
        Predicate,
    },
    fuchsia_zircon::Duration,
    futures::{future::BoxFuture, FutureExt},
    std::{
        ops::{Deref, DerefMut},
        sync::Arc,
    },
    test_harness::{SharedState, TestHarness},
};

use crate::{
    core_realm::{CoreRealm, SHARED_STATE_INDEX},
    host_watcher::ActivatedFakeHost,
    timeout_duration,
};

// Controls the rate at which to snapshot the inspect tree (i.e. update InspectState). Arbitrarily
// set to snapshot the inspect tree every 1 second.
const SNAPSHOT_INSPECT_EVERY_N_SECONDS: Duration = Duration::from_seconds(1);

#[derive(Clone)]
pub struct InspectState {
    /// The moniker of the component whose inspect this tracks. Should be relative to the root realm
    /// component, and each component of the moniker should be separate.
    /// Example: Let's say we have Component A with name "component-a", and Component A has a child
    /// with name "component-b". If we add component A to the RealmBuilder, and we want to monitor
    /// the Inspect state for "component-b", we would set this value to
    /// `vec!["component-a", "component-b"]`.
    // Note that this is not the final moniker used as a component selector; we also have to prepend
    // the realm child's moniker (which is based on the realm_child_name member).
    pub moniker_to_track: Vec<String>,
    /// The Diagnostic Hierarchies of the monitored component (if any)
    pub hierarchies: Vec<DiagnosticsHierarchy>,
    realm_child_name: String,
}

#[derive(Clone)]
pub struct InspectHarness(Expectable<InspectState, AccessProxy>);

impl InspectHarness {
    // Check if there are at least `min_num` hierarchies in our Inspect State. If so, return the
    // inspect state, otherwise return Error.
    pub async fn expect_n_hierarchies(&self, min_num: usize) -> Result<InspectState, Error> {
        self.when_satisfied(
            Predicate::<InspectState>::predicate(
                move |state| state.hierarchies.len() >= min_num,
                "Expected number of hierarchies received",
            ),
            timeout_duration(),
        )
        .await
    }

    fn get_component_selector(&self) -> ComponentSelector {
        let realm_child_moniker = format!("realm_builder\\:{}", self.read().realm_child_name);
        let mut complete_moniker = self.read().moniker_to_track;
        complete_moniker.insert(0, realm_child_moniker);
        return ComponentSelector::new(complete_moniker);
    }
}

impl Deref for InspectHarness {
    type Target = Expectable<InspectState, AccessProxy>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for InspectHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

pub async fn handle_inspect_updates(harness: InspectHarness) -> Result<(), Error> {
    loop {
        if harness.read().moniker_to_track.len() > 0 {
            let mut reader = ArchiveReader::new();
            let _ = reader.add_selector(harness.get_component_selector());
            harness.write_state().hierarchies = reader
                .snapshot::<Inspect>()
                .await?
                .into_iter()
                .flat_map(|result| result.payload)
                .collect();
            harness.notify_state_changed();
        }
        fuchsia_async::Timer::new(SNAPSHOT_INSPECT_EVERY_N_SECONDS.after_now()).await;
    }
}

impl TestHarness for InspectHarness {
    type Env = (ActivatedFakeHost, Arc<CoreRealm>);
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init(
        shared_state: &Arc<SharedState>,
    ) -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        let shared_state = shared_state.clone();
        async move {
            let realm =
                shared_state.get_or_insert_with(SHARED_STATE_INDEX, CoreRealm::create).await?;
            // Publish emulator to driver stack
            let fake_host = ActivatedFakeHost::new(realm.clone()).await?;

            let access_proxy = realm
                .instance()
                .connect_to_protocol_at_exposed_dir::<AccessMarker>()
                .context("Failed to connect to Access service")?;
            let state = InspectState {
                moniker_to_track: Vec::new(),
                hierarchies: Vec::new(),
                realm_child_name: realm.instance().child_name().to_string(),
            };

            let harness = InspectHarness(expectable(state, access_proxy));
            let run_inspect = handle_inspect_updates(harness.clone()).boxed();
            Ok((harness, (fake_host, realm), run_inspect))
        }
        .boxed()
    }

    fn terminate((emulator, realm): Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        // The realm must be kept alive in order for ActivatedFakeHost::release to work properly.
        async move {
            let _realm = realm;
            emulator.release().await
        }
        .boxed()
    }
}
