// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, Error},
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
        bail!("A test failed.");
    } else {
        println!("All tests passed.");
    }
    Ok(())
}

fn run_command(inspect: &str, configs: &Vec<&str>) -> Result<Output, Error> {
    let mut args = Vec::new();
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
        Err(err) => bail!("Command didn't run: {:?}", err.kind()),
    }
}

// If there's an error, print a summary and return true; otherwise, false.
fn report_error(
    inspect: &str,
    config: &Vec<&str>,
    rc: i32,
    substring: &str,
    should_output: bool,
) -> bool {
    match run_command(inspect, config) {
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
                    "Bad return code {:?} (expected {}) from {} and {:?}; output:\n'{}'\n",
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
        (@internal $name:expr, $inspect:expr, $config:expr, $rc:expr, $string:expr,
                $contains:expr) => {
            println!("Testing: {}", $name);
            if report_error($inspect, $config, $rc, $string, $contains) {
                failed = true;
            }
        };
        ($name:expr, $inspect:expr, $config:expr, $rc:expr, not $out:expr) => {
            should!(@internal $name, $inspect, $config, $rc, $out, false);
        };
        ($name:expr, $inspect:expr, $config:expr, $rc:expr, $out:expr) => {
            should!(@internal $name, $inspect, $config, $rc, $out, true);
        };
    }
    should!(
        "Report missing inspect.json",
        "not_found_file",
        &vec!["sample.triage"],
        0,
        "Couldn't read Inspect file"
    );
    should!(
        "Report missing config file",
        "inspect.json",
        &vec!["cfg2"],
        0,
        "Couldn't read config file"
    );
    should!("Successfully read correct files",
        "inspect.json", &vec!["other.triage", "sample.triage"], 0, not "Couldn't");
    should!(
        "Use namespace in actions",
        "inspect.json",
        &vec!["other.triage", "sample.triage"],
        0,
        "Warning: 'act1' in 'other' detected 'yes on A!': 'sample::c1' was true"
    );
    should!(
        "Use namespace in metrics",
        "inspect.json",
        &vec!["other.triage", "sample.triage"],
        0,
        "Warning: 'some_disk' in 'sample' detected 'Used some of disk': 'tiny' was true"
    );
    should!(
        "Fail on missing namespace",
        "inspect.json",
        &vec!["sample.triage"],
        0,
        "Bad namespace"
    );
    failed
}
