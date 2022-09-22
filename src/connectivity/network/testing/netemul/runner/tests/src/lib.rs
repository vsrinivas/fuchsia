// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use assert_matches::assert_matches;
use fidl_fuchsia_test_manager as ftest_manager;
use test_manager_test_lib::RunEvent;

pub async fn run_test(test_url: &str) -> Result<(Vec<RunEvent>, Vec<String>), anyhow::Error> {
    let run_builder =
        fuchsia_component::client::connect_to_protocol::<ftest_manager::RunBuilderMarker>()
            .expect("connect to test manager protocol");
    let builder = test_manager_test_lib::TestBuilder::new(run_builder).filter_debug_data();
    let suite_instance = builder
        .add_suite(test_url, ftest_manager::RunOptions::EMPTY)
        .await
        .expect("create suite instance");

    let (test_run_result, test_suite_result) = futures_util::future::join(
        builder.run(),
        test_runners_test_lib::process_events(suite_instance, true),
    )
    .await;
    // `TestBuilder::run` returns a `Vec` of `TestRunEvent`s, which only ever
    // include debug data currently, so expect this to be empty.
    let test_run_events = test_run_result.expect("builder execution failed");
    assert_matches!(&test_run_events[..], []);

    test_suite_result
}
