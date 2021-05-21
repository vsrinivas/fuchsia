// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    futures::{channel::mpsc, prelude::*},
    test_executor::{
        DisabledTestHandling, SuiteInstance, SuiteInstanceOpts, TestEvent, TestRunOptions,
    },
};

pub async fn run_test(
    test_url: &str,
    disabled_tests: DisabledTestHandling,
    parallel: Option<u16>,
    arguments: Vec<String>,
) -> Result<Vec<TestEvent>, Error> {
    let harness = test_runners_test_lib::connect_to_test_manager().await?;
    let suite_instance = SuiteInstance::new(SuiteInstanceOpts {
        harness: &harness,
        test_url,
        force_log_protocol: None,
    })
    .await?;

    let (sender, recv) = mpsc::channel(1);

    let run_options = TestRunOptions { disabled_tests, parallel, arguments };

    let (events, ()) = futures::future::try_join(
        recv.collect::<Vec<_>>().map(Ok),
        suite_instance.run_and_collect_results(sender, None, run_options),
    )
    .await
    .context("running test")?;

    Ok(test_runners_test_lib::process_events(events, true))
}
