// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_control::{ControlMarker, ControlProxy},
    fuchsia_async::DurationExt,
    fuchsia_bluetooth::expectation::{
        asynchronous::{ExpectableState, ExpectableStateExt, ExpectationHarness},
        Predicate,
    },
    fuchsia_inspect::{self as inspect, reader::NodeHierarchy},
    fuchsia_zircon::DurationNum,
    futures::{
        future::{self, BoxFuture},
        FutureExt,
    },
};

use crate::{harness::TestHarness, tests::timeout_duration};

const RETRY_TIMEOUT_SECONDS: i64 = 1;

pub async fn expect_hierarchies(harness: &InspectHarness) -> Result<InspectState, Error> {
    harness
        .when_satisfied(
            Predicate::<InspectState>::new(
                move |state| !state.hierarchies.is_empty(),
                Some("Hierarchy non-empty"),
            ),
            timeout_duration(),
        )
        .await
}

#[derive(Default, Clone)]
pub struct InspectState {
    pub hierarchies: Vec<NodeHierarchy>,
}

pub type InspectHarness = ExpectationHarness<InspectState, ControlProxy>;

pub async fn handle_inspect_updates(harness: InspectHarness) -> Result<(), Error> {
    loop {
        let fetcher = inspect::testing::InspectDataFetcher::new()
            .add_selector(inspect::testing::ComponentSelector::new(vec!["bt-gap.cmx".to_string()]));
        harness.write_state().hierarchies = fetcher.get().await?;
        harness.notify_state_changed();
        fuchsia_async::Timer::new(RETRY_TIMEOUT_SECONDS.seconds().after_now()).await;
    }
}

pub async fn new_inspect_harness() -> Result<InspectHarness, Error> {
    let proxy = fuchsia_component::client::connect_to_service::<ControlMarker>()
        .context("Failed to connect to Control service")?;

    let inspect_harness = InspectHarness::new(proxy);
    Ok(inspect_harness)
}

impl TestHarness for InspectHarness {
    type Env = ();
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let harness = new_inspect_harness().await?;
            let run_inspect = handle_inspect_updates(harness.clone()).boxed();
            Ok((harness, (), run_inspect))
        }
        .boxed()
    }

    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}
