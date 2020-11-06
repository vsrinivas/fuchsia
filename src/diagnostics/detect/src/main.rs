// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `triage-detect` is responsible for auto-triggering crash reports in Fuchsia.

// TODO(fxbug.dev/61333): Several things
// need to be answered and implemented before this is deployed.
//
// How and whether to gate/space crash report requests - should we queue them (with limited slots)
//  the way PowerManager code does? (probably not)
// Should we throttle crash report requests (N per day) and where to enforce this?
// Do signatures need to be unique for each action? Namespaced between files?
// Integration test
// Restrict signature to lowercase-and-hyphens.

mod delay_tracker;
mod diagnostics;
mod snapshot;
mod triage_shim;

use {
    anyhow::{bail, Context, Error},
    argh::FromArgs,
    delay_tracker::DelayTracker,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    glob::glob,
    injectable_time::UtcTime,
    log::{error, info, warn},
    snapshot::SnapshotRequest,
    std::collections::HashMap,
};

const MINIMUM_CHECK_TIME_NANOS: i64 = 60 * 1_000_000_000;
const CONFIG_GLOB: &str = "/config/data/*";
const SIGNATURE_PREFIX: &str = "fuchsia-detect-";
const MINIMUM_SIGNATURE_INTERVAL_NANOS: i64 = 3600 * 1_000_000_000;

/// Command line args
#[derive(FromArgs, Debug)]
struct CommandLine {
    /// how often to scan Diagnostic data
    #[argh(option)]
    check_every: Option<String>,

    /// ignore minimum times for testing. Never check in code with this flag set.
    #[argh(switch)]
    test_only: bool,
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum Mode {
    Test,
    Production,
}

fn load_configuration_files() -> Result<HashMap<String, String>, Error> {
    fn file_stem(file_path: &std::path::PathBuf) -> Result<String, Error> {
        if let Some(s) = file_path.file_stem() {
            if let Some(s) = s.to_str() {
                return Ok(s.to_owned());
            }
        }
        bail!("Bad path {:?} - can't find file_stem", file_path)
    }

    let mut file_contents = HashMap::new();
    for file_path in glob(CONFIG_GLOB)? {
        let file_path = file_path?;
        let stem = file_stem(&file_path)?;
        let config_text = std::fs::read_to_string(file_path)?;
        file_contents.insert(stem, config_text);
    }
    Ok(file_contents)
}

fn load_command_line() -> Result<CommandLine, Error> {
    // We can't just use the one-line argh parse, because that writes to stdout
    // and stdout doesn't currently work in v2 components. Instead, grab and
    // log the output.
    let arg_strings = std::env::args().collect::<Vec<_>>();
    let arg_strs: Vec<&str> = arg_strings.iter().map(|s| s.as_str()).collect();
    match CommandLine::from_args(&[arg_strs[0]], &arg_strs[1..]) {
        Ok(args) => Ok(args),
        Err(output) => {
            for line in output.output.split("\n") {
                warn!("CmdLine: {}", line);
            }
            match output.status {
                Ok(()) => bail!("Exited as requested by command line args"),
                Err(()) => bail!("Exited due to bad command line args"),
            }
        }
    }
}

/// appropriate_check_interval determines the interval to check diagnostics, or signals error.
///
/// If the command line arg is empty, the interval is set to MINIMUM_CHECK_TIME_NANOS.
/// If the command line can't be evaluated to an integer, an error is returned.
/// If the integer is below minimum and mode isn't Test, an error is returned.
/// If a valid integer is determined, it is returned as a zx::Duration.
fn appropriate_check_interval(
    command_line_option: &Option<String>,
    mode: &Mode,
) -> Result<zx::Duration, Error> {
    let check_every = match &command_line_option {
        None => MINIMUM_CHECK_TIME_NANOS,
        Some(expression) => triage_shim::evaluate_int_math(&expression).or_else(|e| {
            bail!("Check_every argument must be Minutes(n), Hours(n), etc. but: {}", e)
        })?,
    };
    if check_every < MINIMUM_CHECK_TIME_NANOS && *mode != Mode::Test {
        bail!(
            "Minimum time to check is {} seconds; {} nanos is too small",
            MINIMUM_CHECK_TIME_NANOS / 1_000_000_000,
            check_every
        );
    }
    info!(
        "Checking every {} seconds from command line '{:?}'",
        check_every / 1_000_000_000,
        command_line_option
    );
    Ok(zx::Duration::from_nanos(check_every))
}

// on_error logs any errors from `value` and then returns a Result.
// value must return a Result; error_message must contain one {} to put the error in.
macro_rules! on_error {
    ($value:expr, $error_message:expr) => {
        $value.or_else(|e| {
            let message = format!($error_message, e);
            error!("{}", message);
            bail!("{}", message)
        })
    };
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["detect"]).context("initializing logging").unwrap();
    let args = on_error!(load_command_line(), "Command line error: {}")?;
    let mode = match args.test_only {
        true => Mode::Test,
        false => Mode::Production,
    };
    let check_every = on_error!(
        appropriate_check_interval(&args.check_every, &mode),
        "Invalid command line arg for check time: {}"
    )?;
    let configuration =
        on_error!(load_configuration_files(), "Error reading configuration files: {}")?;
    let triage_engine = on_error!(
        triage_shim::TriageLib::new(configuration),
        "Failed to parse Detect configuration files: {}"
    )?;
    info!("Loaded and parsed .triage files");
    let selectors = triage_engine.selectors();
    let mut diagnostic_source = diagnostics::DiagnosticFetcher::create(selectors)?;
    let snapshot_service = snapshot::CrashReportHandlerBuilder::new().build()?;
    let system_time = UtcTime::new();
    let mut delay_tracker = DelayTracker::new(&system_time, &mode);

    // Start the first scan as soon as the program starts, via the "missed deadline" logic below.
    let mut next_check_time = fasync::Time::INFINITE_PAST;
    loop {
        if next_check_time < fasync::Time::now() {
            // We missed a deadline, so don't wait at all; start the check. But first
            // schedule the next check time at now() + check_every.
            if next_check_time != fasync::Time::INFINITE_PAST {
                warn!(
                    "Missed diagnostic check deadline {:?} by {:?} nanos",
                    next_check_time,
                    fasync::Time::now() - next_check_time
                );
            }
            next_check_time = fasync::Time::now() + check_every;
        } else {
            // Wait until time for the next check.
            fasync::Timer::new(next_check_time).await;
            // Now it should be approximately next_check_time o'clock. To avoid drift from
            // delays, calculate the next check time by adding check_every to the current
            // next_check_time.
            next_check_time += check_every;
        }
        let diagnostics = diagnostic_source.get_diagnostics().await;
        let diagnostics = match diagnostics {
            Ok(diagnostics) => diagnostics,
            Err(e) => {
                // This happens when the integration tester runs out of Inspect data.
                if mode != Mode::Test {
                    error!("Fetching diagnostics failed: {}", e);
                }
                continue;
            }
        };
        let snapshot_requests = triage_engine.evaluate(diagnostics);
        for snapshot in snapshot_requests.into_iter() {
            if delay_tracker.ok_to_send(&snapshot) {
                let signature = format!("{}{}", SIGNATURE_PREFIX, snapshot.signature);
                if let Err(e) = snapshot_service.request_snapshot(SnapshotRequest::new(signature)) {
                    error!("Snapshot request failed: {}", e);
                }
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn verify_appropriate_check_interval() -> Result<(), Error> {
        let error_a = Some("a".to_string());
        let error_empty = Some("".to_string());
        let raw_1 = Some("1".to_string());
        let raw_1_result = zx::Duration::from_nanos(1);
        let short_time = Some(format!("Nanos({})", MINIMUM_CHECK_TIME_NANOS - 1));
        let short_time_result = zx::Duration::from_nanos(MINIMUM_CHECK_TIME_NANOS - 1);
        let minimum_time = Some(format!("Nanos({})", MINIMUM_CHECK_TIME_NANOS));
        let minimum_time_result = zx::Duration::from_nanos(MINIMUM_CHECK_TIME_NANOS);
        let long_time = Some(format!("Nanos({})", MINIMUM_CHECK_TIME_NANOS + 1));
        let long_time_result = zx::Duration::from_nanos(MINIMUM_CHECK_TIME_NANOS + 1);

        assert!(appropriate_check_interval(&error_a, &Mode::Test).is_err());
        assert!(appropriate_check_interval(&error_empty, &Mode::Test).is_err());
        assert_eq!(appropriate_check_interval(&None, &Mode::Test)?, minimum_time_result);
        assert_eq!(appropriate_check_interval(&raw_1, &Mode::Test)?, raw_1_result);
        assert_eq!(appropriate_check_interval(&short_time, &Mode::Test)?, short_time_result);
        assert_eq!(appropriate_check_interval(&minimum_time, &Mode::Test)?, minimum_time_result);
        assert_eq!(appropriate_check_interval(&long_time, &Mode::Test)?, long_time_result);

        assert!(appropriate_check_interval(&error_a, &Mode::Production).is_err());
        assert!(appropriate_check_interval(&error_empty, &Mode::Production).is_err());
        assert_eq!(appropriate_check_interval(&None, &Mode::Production)?, minimum_time_result);
        assert!(appropriate_check_interval(&raw_1, &Mode::Production).is_err());
        assert!(appropriate_check_interval(&short_time, &Mode::Production).is_err());
        assert_eq!(
            appropriate_check_interval(&minimum_time, &Mode::Production)?,
            minimum_time_result
        );
        assert_eq!(appropriate_check_interval(&long_time, &Mode::Test)?, long_time_result);
        assert_eq!(appropriate_check_interval(&long_time, &Mode::Production)?, long_time_result);
        Ok(())
    }
}
