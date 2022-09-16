// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_test_manager::RunOptions,
    fuchsia_async as fasync,
    test_manager_test_lib::RunEvent,
};

pub async fn run_test(
    test_url: &str,
    run_disabled_tests: bool,
    parallel: Option<u16>,
    arguments: Vec<String>,
) -> Result<(Vec<RunEvent>, Vec<String>), Error> {
    let run_builder = test_runners_test_lib::connect_to_test_manager().await?;
    let builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance = builder
        .add_suite(
            test_url,
            RunOptions {
                run_disabled_tests: Some(run_disabled_tests),
                parallel,
                arguments: Some(arguments),
                timeout: None,
                case_filters_to_run: None,
                log_iterator: None,
                ..RunOptions::EMPTY
            },
        )
        .await
        .context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { builder.run().await });
    let ret = test_runners_test_lib::process_events(suite_instance, true).await?;
    builder_run.await.context("builder execution failed")?;
    Ok(ret)
}
