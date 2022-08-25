// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Calibration, LedConfig, LedMap, Parameters, Rgbc};
use crate::light_sensor::types::{CalibrationConfiguration, FileLoader};
use anyhow::format_err;
use async_trait::async_trait;
use fasync::TestExecutor;
use fuchsia_async as fasync;
use futures::FutureExt;
use std::{collections::HashMap, path::Path, task::Poll};
use test_case::test_case;

#[test_case(
    0 => Ok(Rgbc { red: 2, green: 3, blue: 4, clear: 5 });
    "successful map"
)]
#[test_case(
    1 => Err(String::from("Failed to map red\n\nCaused by:\n    my_error"));
    "red error map"
)]
#[test_case(
    2 => Err(String::from("Failed to map green\n\nCaused by:\n    my_error"));
    "green error map"
)]
#[test_case(
    3 => Err(String::from("Failed to map blue\n\nCaused by:\n    my_error"));
    "blue error map"
)]
#[test_case(
    4 => Err(String::from("Failed to map clear\n\nCaused by:\n    my_error"));
    "clear error map"
)]
#[fuchsia::test]
fn rgbc_async_mapped(n: u8) -> Result<Rgbc<u8>, String> {
    let rgbc = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };

    let mut result_fut = rgbc
        .async_mapped(|c| async move {
            if c == n {
                Err(format_err!("my_error"))
            } else {
                Ok(c + 1)
            }
        })
        .boxed();

    let mut executor = TestExecutor::new().expect("should be able to creat test excutor");
    let result = match executor.run_until_stalled(&mut result_fut) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("Simple map should not have failed"),
    };
    result.map_err(|e| format!("{:?}", e))
}

#[fuchsia::test]
fn rgbc_map() {
    let rgbc = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let rgbc = rgbc.map(|c| c + 1);
    assert_eq!(rgbc, Rgbc { red: 2, green: 3, blue: 4, clear: 5 });
}

#[fuchsia::test]
fn rgbc_multi_map() {
    let left = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let right = Rgbc { red: 5, green: 6, blue: 7, clear: 8 };
    let rgbc = Rgbc::multi_map(left, right, |l, r| l + r);
    assert_eq!(rgbc, Rgbc { red: 6, green: 8, blue: 10, clear: 12 });
}

#[fuchsia::test]
fn rgbc_fold() {
    let rgbc = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let result = rgbc.fold(1, |acc, c| acc * c);
    assert_eq!(result, 24);
}

#[fuchsia::test]
fn rgbc_add() {
    let left = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let right = Rgbc { red: 5, green: 6, blue: 7, clear: 8 };
    let rgbc = left + right;
    assert_eq!(rgbc, Rgbc { red: 6, green: 8, blue: 10, clear: 12 });
}

#[fuchsia::test]
fn rgbc_sub() {
    let left = Rgbc { red: 5, green: 7, blue: 9, clear: 11 };
    let right = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let rgbc = left - right;
    assert_eq!(rgbc, Rgbc { red: 4, green: 5, blue: 6, clear: 7 });
}

#[fuchsia::test]
fn rgbc_mul() {
    let left = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let right = Rgbc { red: 2, green: 3, blue: 4, clear: 5 };
    let rgbc = left * right;
    assert_eq!(rgbc, Rgbc { red: 2, green: 6, blue: 12, clear: 20 });
}

#[fuchsia::test]
fn rgbc_div() {
    let left = Rgbc { red: 2, green: 6, blue: 12, clear: 20 };
    let right = Rgbc { red: 1, green: 2, blue: 3, clear: 4 };
    let rgbc = left / right;
    assert_eq!(rgbc, Rgbc { red: 2, green: 3, blue: 4, clear: 5 });
}

/// Helper function that ensures both parameter fields match within epsilon range.
fn parameters_close_enough(left: Parameters, right: Parameters) -> bool {
    (left.slope - right.slope).abs() <= std::f32::EPSILON
        && (left.intercept - right.intercept).abs() <= std::f32::EPSILON
}

/// Helper function that ensures all parameters in both [Rgbc] structs match within epsilon range.
fn rgbc_parameters_close_enough(left: Rgbc<Parameters>, right: Rgbc<Parameters>) -> bool {
    Rgbc::match_all(left, right, parameters_close_enough)
}

#[test_case(None, None; "happy path")]
#[test_case(
    Some("leda-r"), Some(&["Failed to map \"leda\"'s rgbc field", "red"]); "leda red failure"
)]
#[test_case(
    Some("leda-g"), Some(&["Failed to map \"leda\"'s rgbc field", "green"]); "leda green failure"
)]
#[test_case(
    Some("leda-b"), Some(&["Failed to map \"leda\"'s rgbc field", "blue"]); "leda blue failure"
)]
#[test_case(
    Some("leda-c"), Some(&["Failed to map \"leda\"'s rgbc field", "clear"]); "leda clear failure"
)]
#[test_case(
    Some("ledg-r"), Some(&["Failed to map \"ledg\"'s rgbc field", "red"]); "ledg red failure"
)]
#[test_case(
    Some("ledg-g"), Some(&["Failed to map \"ledg\"'s rgbc field", "green"]); "ledg green failure"
)]
#[test_case(
    Some("ledg-b"), Some(&["Failed to map \"ledg\"'s rgbc field", "blue"]); "ledg blue failure"
)]
#[test_case(
    Some("ledg-c"), Some(&["Failed to map \"ledg\"'s rgbc field", "clear"]); "ledg clear failure"
)]
#[test_case(Some("off-r"), Some(&["Failed to map off rgbc", "red"]); "off red failure")]
#[test_case(Some("off-g"), Some(&["Failed to map off rgbc", "green"]); "off green failure")]
#[test_case(Some("off-b"), Some(&["Failed to map off rgbc", "blue"]); "off blue failure")]
#[test_case(Some("off-c"), Some(&["Failed to map off rgbc", "clear"]); "off clear failure")]
#[test_case(Some("all_on-r"), Some(&["Failed to map all_on rgbc", "red"]); "all_on red failure")]
#[test_case(Some("all_on-g"), Some(&["Failed to map all_on rgbc", "green"]); "all_on green failure")]
#[test_case(Some("all_on-b"), Some(&["Failed to map all_on rgbc", "blue"]); "all_on blue failure")]
#[test_case(Some("all_on-c"), Some(&["Failed to map all_on rgbc", "clear"]); "all_on clear failure")]
#[fuchsia::test]
fn calibration_new(failure_file: Option<&str>, error_strings: Option<&[&str]>) {
    let configuration = CalibrationConfiguration {
        leds: vec![
            LedConfig {
                name: "leda".to_string(),
                rgbc: Rgbc { red: "leda-r", green: "leda-g", blue: "leda-b", clear: "leda-c" }
                    .map(|s| s.to_string()),
            },
            LedConfig {
                name: "ledg".to_string(),
                rgbc: Rgbc { red: "ledg-r", green: "ledg-g", blue: "ledg-b", clear: "ledg-c" }
                    .map(|s| s.to_string()),
            },
        ],
        off: Rgbc { red: "off-r", green: "off-g", blue: "off-b", clear: "off-c" }
            .map(|s| s.to_string()),
        all_on: Rgbc { red: "all_on-r", green: "all_on-g", blue: "all_on-b", clear: "all_on-c" }
            .map(|s| s.to_string()),
        golden_calibration_params: Rgbc {
            red: Parameters { slope: 32.0, intercept: 1.5 },
            green: Parameters { slope: 33.0, intercept: 2.5 },
            blue: Parameters { slope: 34.0, intercept: 3.5 },
            clear: Parameters { slope: 35.0, intercept: 4.5 },
        },
    };

    let mut executor = TestExecutor::new().expect("should get test executor");

    struct FakeFileLoader<'a> {
        failure_file: Option<&'a str>,
    }

    #[async_trait(?Send)]
    impl FileLoader for FakeFileLoader<'_> {
        async fn load_file(&self, path: &Path) -> Result<String, anyhow::Error> {
            if let Some(file_path) = self.failure_file {
                if path == AsRef::<Path>::as_ref(&file_path) {
                    return Err(format_err!("my_error"));
                }
            }

            // unwrap safe since we only call this method with strings.
            let parts: Vec<_> = path.to_str().unwrap().split('-').collect();
            assert_eq!(parts.len(), 2);
            let file_type = parts[0];
            let color_channel = parts[1];
            let multiplier = match file_type {
                "leda" => 1.0,
                "ledg" => 2.0,
                "off" => 3.0,
                "all_on" => 4.0,
                _ => panic!("File type {:?} not expected", file_type),
            };
            let additional = match color_channel {
                "r" => 1.0,
                "g" => 2.0,
                "b" => 3.0,
                "c" => 4.0,
                _ => panic!("Color channel {:?} not expected", color_channel),
            };
            let slope = 10.0 * multiplier + additional;
            let intercept = slope + 50.0;

            // Follows the format:
            // cal_version samples_per_step
            //
            // linear_fit_slope linear_fit_intercept
            //
            // lux measurement
            // # 4 more times
            Ok(format!(
                "
                1 10

                {} {}

                0.002 33.0
                1.212 36.0
                10.492 162.6
                50.3 777.9
                250.53 3880.9
                ",
                slope, intercept
            ))
        }
    }

    let mut calibration_result_fut =
        Calibration::new(configuration, FakeFileLoader { failure_file }).boxed_local();
    let calibration_result = match executor.run_until_stalled(&mut calibration_result_fut) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("calibration new should not stall in this test"),
    };

    fn leds_match(left: &LedMap, right: &LedMap) -> bool {
        if left.len() != right.len() {
            return false;
        }

        left.iter().all(|(name, left_rgbc)| {
            right
                .get(name)
                .map(|right_rgbc| rgbc_parameters_close_enough(*left_rgbc, *right_rgbc))
                .unwrap_or(false)
        })
    }

    match (calibration_result, error_strings) {
        (Ok(calibration), None) => {
            assert!(rgbc_parameters_close_enough(
                calibration.off(),
                Rgbc {
                    red: Parameters { slope: 31.0, intercept: 81.0 },
                    green: Parameters { slope: 32.0, intercept: 82.0 },
                    blue: Parameters { slope: 33.0, intercept: 83.0 },
                    clear: Parameters { slope: 34.0, intercept: 84.0 },
                }
            ));
            assert!(rgbc_parameters_close_enough(
                calibration.all_on(),
                Rgbc {
                    red: Parameters { slope: 41.0, intercept: 91.0 },
                    green: Parameters { slope: 42.0, intercept: 92.0 },
                    blue: Parameters { slope: 43.0, intercept: 93.0 },
                    clear: Parameters { slope: 44.0, intercept: 94.0 },
                }
            ));
            assert!(Rgbc::match_all(
                calibration.calibrated_slope(),
                // Values below should match
                // configuration.golden_calibration_params.<color>.slope / off.<color>.slope
                Rgbc {
                    // 32.0 / 31.0
                    red: 1.032258,
                    // 33.0 / 32.0
                    green: 1.03125,
                    // 34.0 / 33.0
                    blue: 1.030303,
                    // 35.0 / 34.0
                    clear: 1.0294118,
                },
                |l, r| (l - r).abs() <= std::f32::EPSILON
            ));
            assert!(leds_match(
                calibration.leds(),
                &HashMap::from([
                    (
                        "leda".to_string(),
                        Rgbc {
                            red: Parameters { slope: 11.0, intercept: 61.0 },
                            green: Parameters { slope: 12.0, intercept: 62.0 },
                            blue: Parameters { slope: 13.0, intercept: 63.0 },
                            clear: Parameters { slope: 14.0, intercept: 64.0 },
                        }
                    ),
                    (
                        "ledg".to_string(),
                        Rgbc {
                            red: Parameters { slope: 21.0, intercept: 71.0 },
                            green: Parameters { slope: 22.0, intercept: 72.0 },
                            blue: Parameters { slope: 23.0, intercept: 73.0 },
                            clear: Parameters { slope: 24.0, intercept: 74.0 },
                        }
                    )
                ])
            ));
        }
        (Err(e), Some(errors)) => {
            let formatted_err = format!("{:?}", e);

            assert!(errors.len() > 0);
            for error in errors {
                if !formatted_err.contains(error) {
                    panic!("Expected `{:?}` to contain `{:?}`", formatted_err, error);
                }
            }
        }
        (Ok(calibration), Some(errors)) => {
            panic!(
                "Expected errors with text {:?}, but found successful result: {:?}",
                errors, calibration
            );
        }
        (Err(e), None) => panic!("Expected successful result, but found error: {:?}", e),
    }
}

#[test_case("   1   10  1.0  2.0  extra ignored   ", None; "trims whitespace")]
#[test_case("1 10\n1.0 2.0", None; "happy path")]
#[test_case("10\n1.0 2.0", Some(&["Missing intercept"]); "missing cal version")]
#[test_case("1 10\n1.0", Some(&["Missing intercept"]); "missing intercept")]
#[test_case("1 10", Some(&["Missing slope"]); "missing slope and intercept")]
#[test_case("1 10\nnot_f32 2.0", Some(&["Failed to parse slope"]); "bad slope format")]
#[test_case("1 10\n1.0 not_f32", Some(&["Failed to parse intercept"]); "bad intercept format")]
#[test_case("1 10\nNaN 2.0", Some(&["Slope must not be NaN or Infinity"]); "nan slope format")]
#[test_case(
    "1 10\n1.0 NaN", Some(&["Intercept must not be NaN or Infinity"]); "nan intercept format"
)]
#[test_case(
    "1 10\ninf 2.0", Some(&["Slope must not be NaN or Infinity"]); "infinity slope format"
)]
#[test_case(
    "1 10\n1.0 inf", Some(&["Intercept must not be NaN or Infinity"]); "infinity intercept format"
)]
#[test_case(
    "1 10\n-inf 2.0", Some(&["Slope must not be NaN or Infinity"]); "negative infinity slope format"
)]
#[test_case(
    "1 10\n1.0 -inf", Some(&["Intercept must not be NaN or Infinity"]);
    "negative infinty intercept format"
)]
#[fuchsia::test]
fn calibration_parse_file_trims(input: &str, errors: Option<&[&str]>) {
    struct SimpleFileLoader<'a> {
        input: &'a str,
    }

    #[async_trait(?Send)]
    impl FileLoader for SimpleFileLoader<'_> {
        async fn load_file(&self, _: &Path) -> Result<String, anyhow::Error> {
            Ok(String::from(self.input))
        }
    }

    let simple_file_loader = SimpleFileLoader { input };
    let mut result_fut =
        Calibration::parse_file("not_important", &simple_file_loader).boxed_local();
    let mut executor = TestExecutor::new().expect("should get test executor");
    let result = match executor.run_until_stalled(&mut result_fut) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("parsing file should not stall in this test"),
    };
    match (result, errors) {
        (Ok(parameters), None) => {
            assert!(parameters_close_enough(parameters, Parameters { slope: 1.0, intercept: 2.0 }))
        }
        (Err(err), Some(errors)) => {
            let formatted_err = format!("{:?}", err);
            for error in errors {
                assert!(
                    formatted_err.contains(error),
                    "expected {:?} to contain {:?}",
                    formatted_err,
                    error
                );
            }
        }
        (Ok(parameters), Some(errors)) => {
            panic!("Expected errors {:?}, but got succesful result {:?}", errors, parameters)
        }
        (Err(err), None) => panic!("Expected succesful result, but got error {:?}", err),
    }
}

#[fuchsia::test(allow_stalls = false)]
async fn calibration_parse_file_bad_parse() {
    struct ErrFileLoader;

    #[async_trait(?Send)]
    impl FileLoader for ErrFileLoader {
        async fn load_file(&self, _: &Path) -> Result<String, anyhow::Error> {
            Err(format_err!("oh no"))
        }
    }

    let result = Calibration::parse_file("not_important", &ErrFileLoader).await;
    assert!(result.is_err());
    assert!(result.map_err(|e| format!("{:?}", e)).unwrap_err().contains("oh no"));
}
