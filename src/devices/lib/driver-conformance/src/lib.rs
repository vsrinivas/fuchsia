// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;
pub mod parser;

use {
    anyhow::Result,
    args::{ConformanceCommand, ConformanceSubCommand},
    driver_connector::DriverConnector,
    errors::ffx_bail,
    ffx_config, fidl_fuchsia_driver_development as fdd, fidl_fuchsia_test_manager as ftm,
    fuchsia_driver_dev::{
        get_devices_by_driver, get_driver_by_device, get_driver_by_libname, Device,
    },
    futures::FutureExt,
    parser::FilterTests,
    serde_json,
    signal_hook::{
        consts::signal::{SIGINT, SIGTERM},
        iterator::Signals,
    },
    std::fs,
    std::io::stdout,
};

impl From<parser::TestInfo> for run_test_suite_lib::TestParams {
    fn from(item: parser::TestInfo) -> Self {
        run_test_suite_lib::TestParams {
            test_url: item.url.to_string(),
            ..run_test_suite_lib::TestParams::default()
        }
    }
}

fn process_test_list(tests: Vec<parser::TestInfo>) -> Result<Vec<run_test_suite_lib::TestParams>> {
    Ok(tests.into_iter().map(Into::into).collect())
}

/// Calls on the `ffx test` library to run the given set of tests.
async fn run_tests(
    tests: Vec<parser::TestInfo>,
    run_proxy: ftm::RunBuilderProxy,
) -> Result<run_test_suite_lib::Outcome> {
    let writer = Box::new(stdout());
    let (cancel_sender, cancel_receiver) = futures::channel::oneshot::channel::<()>();

    let mut signals = Signals::new(&[SIGINT, SIGTERM]).unwrap();
    // signals.forever() is blocking, so we need to spawn a thread rather than use async.
    std::thread::spawn(move || {
        for signal in signals.forever() {
            match signal {
                SIGINT | SIGTERM => {
                    let _ = cancel_sender.send(());
                    break;
                }
                _ => unreachable!(),
            }
        }
    });

    let test_params = process_test_list(tests)?;
    Ok(run_test_suite_lib::run_tests_and_get_outcome(
        run_proxy,
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
    .await)
}

/// Entry-point for the command `ffx driver conformance`.
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
    let mut filtered_tests: Option<Vec<parser::TestInfo>> = None;
    match cmd.subcommand {
        ConformanceSubCommand::Test(subcmd) => {
            // Parse the metadata
            let metadata_str = match subcmd.metadata_path {
                Some(metadata_path_str) => match fs::read_to_string(&metadata_path_str) {
                    Ok(v) => v,
                    Err(e) => ffx_bail!("Unable to parse {}. {}", &metadata_path_str, e),
                },
                None => ffx_bail!("The --metadata-path argument is required for now."),
            };
            let metadata: parser::TestMetadata =
                serde_json::from_str(&metadata_str).expect("Metadata was not valid JSON");

            if let (Some(_), Some(_)) = (&subcmd.device, &subcmd.driver) {
                ffx_bail!("Either --device or --driver is required, but not both.");
            }
            if let (None, None) = (&subcmd.device, &subcmd.driver) {
                ffx_bail!("Either --device or --driver is required.");
            }

            let mut driver_info: Option<fdd::DriverInfo> = None;
            let mut device_list: Option<Vec<Device>> = None;
            let driver_service = driver_connector.get_driver_development_proxy(false).await?;
            if let Some(device) = subcmd.device {
                driver_info = Some(get_driver_by_device(&device, &driver_service).await?);
            }

            if let Some(driver) = subcmd.driver {
                driver_info = Some(get_driver_by_libname(&driver, &driver_service).await?);
                device_list = Some(get_devices_by_driver(&driver, &driver_service).await?);
            } else if let Some(driver) = &driver_info {
                if let Some(driver_libname) = &driver.libname {
                    device_list =
                        Some(get_devices_by_driver(&driver_libname, &driver_service).await?);
                }
            }

            match (&driver_info, &device_list) {
                (Some(driver), Some(devices)) => {
                    println!(
                        "Testing driver {} using {} device(s).",
                        driver.libname.as_ref().unwrap_or(&"".to_string()),
                        devices.len()
                    );
                }
                (Some(_), None) => {
                    ffx_bail!("We were unable to resolve the devices for the given driver.")
                }
                (None, Some(_)) => {
                    ffx_bail!("We were unable to resolve the driver for the given device.")
                }
                _ => ffx_bail!("We were unable to resolve any devices or drivers."),
            }
            // TODO(fxb/113736): Enforce the custom list to be a strict subset of the available
            // tests according to `get_tests_for_driver()`.
            if let Some(custom_list) = subcmd.tests {
                filtered_tests = Some(metadata.tests_by_url(&custom_list.list[..]).unwrap());
            } else if let Some(driver_info) = &driver_info {
                filtered_tests = Some(metadata.tests_by_driver(&driver_info)?);
            }
        }
    }
    match filtered_tests {
        Some(tests) => {
            if tests.is_empty() {
                println!("There were no tests to run for the given command.");
            }
            // We are ignoring the return value because we will read the results from
            // the report generated via `run_test_suite_lib::create_reporter()`.
            let _ = run_tests(tests, driver_connector.get_run_builder_proxy().await?).await;
        }
        None => ffx_bail!("We were unable to create a list of tests to run."),
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_process_test_list() {
        let obj_a = parser::TestInfo {
            url: "abc".to_string(),
            test_types: Box::new([]),
            device_categories: Box::new([]),
            is_automated: true,
        };
        let obj_b = parser::TestInfo {
            url: "def".to_string(),
            test_types: Box::new([]),
            device_categories: Box::new([]),
            is_automated: true,
        };
        let obj_c = parser::TestInfo {
            url: "ghi".to_string(),
            test_types: Box::new([]),
            device_categories: Box::new([]),
            is_automated: true,
        };
        let single = process_test_list(vec![obj_a.clone()]).unwrap();
        assert_eq!(single.len(), 1);
        assert!(
            single.contains(&run_test_suite_lib::TestParams {
                test_url: "abc".to_string(),
                ..run_test_suite_lib::TestParams::default()
            }),
            "Did not find a TestParams instance with test_url: abc"
        );

        let multiple =
            process_test_list(vec![obj_a.clone(), obj_b.clone(), obj_c.clone()]).unwrap();
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

        let obj_url_a = parser::TestInfo {
            url: "fuchsia-pkg://fuchsia.com/fake-test#meta/fake-test.cm".to_string(),
            test_types: Box::new([]),
            device_categories: Box::new([]),
            is_automated: true,
        };
        let obj_url_b = parser::TestInfo {
            url: "fuchsia-pkg://another.domain/dummy_underscore#meta/dummy_underscore.cm"
                .to_string(),
            test_types: Box::new([]),
            device_categories: Box::new([]),
            is_automated: true,
        };
        let urls = process_test_list(vec![obj_url_a, obj_url_b]).unwrap();
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
