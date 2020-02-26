// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    lazy_static::lazy_static,
    std::process::{Command, Output},
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
struct Opt {
    // Target (executable) triage program
    #[structopt(long = "target")]
    target: String,

    // Root of the Fuchsia directory
    #[structopt(long = "root")]
    root: String,
}

lazy_static! {
    static ref COMMAND: String = {
        let Opt { target, .. } = Opt::from_args();
        target
    };
    static ref FUCHSIA_ROOT: String = {
        let Opt { root, .. } = Opt::from_args();
        root
    };
}

fn main() -> Result<(), Error> {
    if look_for_error() {
        return Err(anyhow!("A test failed."));
    } else {
        println!("All tests passed.");
    }
    Ok(())
}

fn run_command(inspect: &str, configs: &Vec<&str>, format: Option<&str>) -> Result<Output, Error> {
    let mut args = Vec::new();
    match format {
        Some(string) => {
            args.push("--output_format".to_owned());
            args.push(string.to_owned());
        }
        None => {}
    }
    args.push("--inspect".to_owned());
    args.push(format!(
        "{}/src/diagnostics/triage/test/inspect/{}",
        FUCHSIA_ROOT.to_string(),
        inspect
    ));
    for config in configs.iter() {
        args.push("--config".to_owned());
        args.push(format!(
            "{}/src/diagnostics/triage/test/config/{}",
            FUCHSIA_ROOT.to_string(),
            config
        ));
    }
    match Command::new(COMMAND.to_string()).args(args).output() {
        Ok(o) => Ok(o),
        Err(err) => return Err(anyhow!("Command didn't run: {:?}", err.kind())),
    }
}

// If there's an error, print a summary and return true; otherwise, false.
fn report_error(
    inspect: &str,
    config: &Vec<&str>,
    format: Option<&str>,
    rc: i32,
    substring: &str,
    should_output: bool,
) -> bool {
    match run_command(inspect, config, format) {
        Ok(output) => {
            let stdout =
                std::str::from_utf8(&output.stdout).unwrap_or("Non-UTF8 return from command");
            if output.status.code() == Some(rc) {
                if stdout.contains(substring) == should_output {
                    return false;
                } else {
                    if should_output {
                        println!(
                            "Output of {} and {:?} did not contain '{}':\n'{}'\n",
                            inspect, config, substring, stdout
                        );
                    } else {
                        println!(
                            "Output of {} and {:?} wrongly contained '{}':\n'{}'\n",
                            inspect, config, substring, stdout
                        );
                    }
                }
            } else {
                println!(
                    "Bad return code {:?} (expected Some({})) from {} and {:?}; output:\n'{}'\n",
                    output.status.code(),
                    rc,
                    inspect,
                    config,
                    stdout
                );
            }
        }
        Err(e) => {
            println!("Command with {} and {:?} failed to run: {}", inspect, config, e);
        }
    }
    return true;
}

fn look_for_error() -> bool {
    let mut failed = false;
    macro_rules! should {
        (@internal $name:expr, $inspect:expr, $config:expr, $format:expr, $rc:expr, $string:expr,
                $contains:expr) => {
            println!("Testing: {}", $name);
            if report_error($inspect, $config, $format, $rc, $string, $contains) {
                failed = true;
            }
        };
        ($name:expr, $inspect:expr, $config:expr, $format:expr, $rc:expr, not $out:expr) => {
            should!(@internal $name, $inspect, $config, $format, $rc, $out, false);
        };
        ($name:expr, $inspect:expr, $config:expr, $format:expr, $rc:expr, $out:expr) => {
            should!(@internal $name, $inspect, $config, $format, $rc, $out, true);
        };
    }
    should!(
        "Report missing inspect.json",
        "not_found_file",
        &vec!["sample.triage"],
        None,
        0,
        "Couldn't read Inspect file"
    );
    should!(
        "Report missing config file",
        "inspect.json",
        &vec!["cfg2"],
        None,
        0,
        "Couldn't read config file"
    );
    should!("Successfully read correct files",
        "inspect.json", &vec!["other.triage", "sample.triage"], None, 0, not "Couldn't");
    should!(
        "Use namespace in actions",
        "inspect.json",
        &vec!["other.triage", "sample.triage"],
        None,
        0,
        "Warning: 'act1' in 'other' detected 'yes on A!': 'sample::c1' was true"
    );
    should!(
        "Use namespace in metrics",
        "inspect.json",
        &vec!["other.triage", "sample.triage"],
        None,
        0,
        "Warning: 'some_disk' in 'sample' detected 'Used some of disk': 'tiny' was true"
    );
    should!(
        "Fail on missing namespace",
        "inspect.json",
        &vec!["sample.triage"],
        None,
        0,
        "Bad namespace"
    );
    should!("Die on bad format arg", "inspect.json", &vec!["sample.triage"], Some("foo"), 1, "");
    should!(
        "Normal output on text format",
        "inspect.json",
        &vec!["other.triage", "sample.triage"],
        Some("text"),
        0,
        "Warning: 'some_disk' in 'sample' detected 'Used some of disk': 'tiny' was true"
    );
    should!(
        "CSV output on csv format",
        "inspect.json",
        &vec!["other.triage", "sample.triage"],
        Some("csv"),
        0,
        "inspect.json,true,false,false,true"
    );
    failed
}
