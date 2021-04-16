// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    futures::{channel::mpsc, prelude::*},
    std::collections::{HashMap, HashSet},
    test_executor::{TestEvent, TestRunOptions},
};

pub async fn run_test(
    test_url: &str,
    test_run_options: TestRunOptions,
) -> Result<Vec<TestEvent>, Error> {
    let harness = test_runners_test_lib::connect_to_test_manager().await?;
    let suite_instance = test_executor::SuiteInstance::new(test_executor::SuiteInstanceOpts {
        harness: &harness,
        test_url,
        force_log_protocol: None,
    })
    .await?;

    let (sender, recv) = mpsc::channel(1);

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        suite_instance.run_and_collect_results(sender, None, test_run_options),
    )
    .await
    .context("running test")?;

    Ok(test_runners_test_lib::process_events(events, false))
}

/// Helper for comparing grouped test events. Produces more readable diffs than diffing the entire
/// two maps.
pub fn assert_events_eq(
    a: &HashMap<Option<String>, Vec<TestEvent>>,
    b: &HashMap<Option<String>, Vec<TestEvent>>,
) {
    let a_keys: HashSet<Option<String>> = b.keys().cloned().collect();
    let b_keys: HashSet<Option<String>> = a.keys().cloned().collect();
    assert_eq!(a_keys, b_keys);
    for key in b.keys() {
        assert_eq!(b.get(key).unwrap(), a.get(key).unwrap())
    }
}
