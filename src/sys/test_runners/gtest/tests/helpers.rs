// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_test_manager::RunOptions,
    fuchsia_async as fasync,
    std::collections::{HashMap, HashSet},
    test_manager_test_lib::{GroupedRunEvents, RunEvent},
};

pub async fn run_test(
    test_url: &str,
    run_options: RunOptions,
) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let run_builder = test_runners_test_lib::connect_to_test_manager().await?;
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance =
        builder.add_suite(test_url, run_options).await.context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let ret = test_runners_test_lib::process_events(suite_instance, false).await;
    builder_run.await.context("builder execution failed")?;
    ret.map(|(mut events, logs)| {
        let () = events.retain(|event| match event {
            RunEvent::CaseStdout { name: _, stdout_message } => {
                // gtest produces this line when tests are randomized. As of
                // this writing, our gtest_main binary *always* randomizes.
                !stdout_message.contains("Note: Randomizing tests' orders with a seed of")
            }
            _ => true,
        });
        (events, logs)
    })
}

/// Helper for comparing grouped test events. Produces more readable diffs than diffing the entire
/// two maps.
pub fn assert_events_eq(
    a: &HashMap<Option<String>, GroupedRunEvents>,
    b: &HashMap<Option<String>, GroupedRunEvents>,
) {
    let a_keys: HashSet<Option<String>> = b.keys().cloned().collect();
    let b_keys: HashSet<Option<String>> = a.keys().cloned().collect();
    assert_eq!(a_keys, b_keys);
    for key in b.keys() {
        assert_eq!(b.get(key).unwrap(), a.get(key).unwrap())
    }
}
