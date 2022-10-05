// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::Result,
    args::{ConformanceCommand, ConformanceSubCommand, TestSubCommand},
    driver_connector::DriverConnector,
    errors::ffx_bail,
    ffx_config,
    futures::FutureExt,
    std::io::stdout,
};

fn process_params(params: Vec<String>) -> Result<Vec<run_test_suite_lib::TestParams>> {
    let params: Vec<_> = params
        .iter()
        .map(|test| run_test_suite_lib::TestParams {
            test_url: test.to_string(),
            ..run_test_suite_lib::TestParams::default()
        })
        .collect();
    Ok(params)
}

pub async fn conformance(
    cmd: ConformanceCommand,
    driver_connector: &dyn DriverConnector,
) -> Result<()> {
    match ffx_config::get("test.driver_conformance_testing").await {
        Ok(true) => {}
        Ok(false) | Err(_) => {
            ffx_bail!(
                "Driver conformance testing is experimental and is subject to breaking changes. \
            To enable driver conformance testing, run \
            'ffx config set test.driver_conformance_testing true'"
            )
        }
    }
    match cmd.subcommand {
        ConformanceSubCommand::Test(subcmd) => match subcmd.subcommand {
            TestSubCommand::Device(_subcmd2) => {
                println!("`ffx driver conformance test device` is WIP");
            }
            TestSubCommand::Category(_subcmd2) => {
                println!("`ffx driver conformance test category` is WIP");
            }
            TestSubCommand::Custom(subcmd2) => {
                println!(
                    "Will run these tests:\n{}\non device `{}`, serving from {}",
                    subcmd2.custom_list,
                    subcmd2.device,
                    subcmd2.cache.unwrap_or("<default>".to_string())
                );
                let writer = Box::new(stdout());
                let (_cancel_sender, cancel_receiver) = futures::channel::oneshot::channel::<()>();
                let test_params = process_params(subcmd2.custom_list.list)?;
                run_test_suite_lib::run_tests_and_get_outcome(
                    driver_connector.get_run_builder_proxy().await?,
                    test_params,
                    run_test_suite_lib::RunParams {
                        timeout_behavior: run_test_suite_lib::TimeoutBehavior::Continue,
                        timeout_grace_seconds: 0,
                        stop_after_failures: None,
                        experimental_parallel_execution: None,
                        accumulate_debug_data: false,
                        log_protocol: None,
                    },
                    None,
                    run_test_suite_lib::create_reporter(false, None, writer)?,
                    cancel_receiver.map(|_| ()),
                )
                .await;
            }
        },
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_process_params_ok() {
        let single = process_params(vec!["abc".to_string()]).unwrap();
        assert_eq!(single.len(), 1);
        assert!(
            single.contains(&run_test_suite_lib::TestParams {
                test_url: "abc".to_string(),
                ..run_test_suite_lib::TestParams::default()
            }),
            "Did not find a TestParams instance with test_url: abc"
        );

        let multiple =
            process_params(vec!["abc".to_string(), "def".to_string(), "ghi".to_string()]).unwrap();
        assert_eq!(multiple.len(), 3);
        for e in vec!["abc", "def", "ghi"] {
            assert!(
                multiple.contains(&run_test_suite_lib::TestParams {
                    test_url: e.to_string(),
                    ..run_test_suite_lib::TestParams::default()
                }),
                "Did not find a TestParams instance with test_url: {}",
                e.to_string()
            );
        }

        let urls = process_params(vec![
            "fuchsia-pkg://fuchsia.com/fake-test#meta/fake-test.cm".to_string(),
            "fuchsia-pkg://another.domain/dummy_underscore#meta/dummy_underscore.cm".to_string(),
        ])
        .unwrap();
        assert_eq!(urls.len(), 2);
        for e in vec![
            "fuchsia-pkg://fuchsia.com/fake-test#meta/fake-test.cm",
            "fuchsia-pkg://another.domain/dummy_underscore#meta/dummy_underscore.cm",
        ] {
            assert!(
                urls.contains(&run_test_suite_lib::TestParams {
                    test_url: e.to_string(),
                    ..run_test_suite_lib::TestParams::default()
                }),
                "Did not find a TestParams instance with test_url: {}",
                e.to_string()
            );
        }
    }
}
