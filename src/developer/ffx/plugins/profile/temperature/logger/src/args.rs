// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::time::Duration};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "logger",
    description = "Controls the metrics-logger component to log temperature. Logged temperature \
samples will be available in syslog, via iquery under core/metrics-logger and via tracing in the \
metrics_logger category.",
    example = "\
To poll temperature sensor every 500 ms indefinitely:

    $ ffx profile temperature logger start --sampling-interval 500ms

To poll temperature sensor every 500 ms and summarize statistics every 1 second for 30 seconds \
with output-samples-to-syslog and output-stats-to-syslog enabled:

    $ ffx profile temperature logger start --sampling-interval 500ms --statistics-interval 1s \
    --output-stats-to-syslog --output-samples-to-syslog -d 30s",
    note = "\
If the metrics-logger component is not available to the target, then this command will not work
properly. Add --with //src/testing/metrics-logger to fx set."
)]
/// Top-level command for "ffx profile temperature logger".
pub struct Command {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    Start(StartCommand),
    Stop(StopCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Start logging on the target
#[argh(subcommand, name = "start")]
pub struct StartCommand {
    #[argh(option, long = "statistics-interval", short = 'l', from_str_fn(parse_duration))]
    /// interval for summarizing statistics; if omitted, statistics is disabled
    pub statistics_interval: Option<Duration>,

    #[argh(option, long = "sampling-interval", short = 's', from_str_fn(parse_duration))]
    /// interval for polling the sensor
    pub sampling_interval: Duration,

    #[argh(switch)]
    /// toggle for logging samples to syslog
    pub output_samples_to_syslog: bool,

    #[argh(switch)]
    /// toggle for logging statistics to syslog
    pub output_stats_to_syslog: bool,

    #[argh(option, long = "duration", short = 'd', from_str_fn(parse_duration))]
    /// duration for which to log; if omitted, logging will continue indefinitely
    pub duration: Option<Duration>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Stop logging on the target
#[argh(subcommand, name = "stop")]
pub struct StopCommand {}

const DURATION_REGEX: &'static str = r"^(\d+)(h|m|s|ms)$";

/// Parses a Duration from string.
fn parse_duration(value: &str) -> Result<Duration, String> {
    let re = regex::Regex::new(DURATION_REGEX).unwrap();
    let captures = re
        .captures(&value)
        .ok_or(format!("Durations must be specified in the form {}", DURATION_REGEX))?;
    let number: u64 = captures[1].parse().unwrap();
    let unit = &captures[2];

    match unit {
        "ms" => Ok(Duration::from_millis(number)),
        "s" => Ok(Duration::from_secs(number)),
        "m" => Ok(Duration::from_secs(number * 60)),
        "h" => Ok(Duration::from_secs(number * 3600)),
        _ => Err(format!(
            "Invalid duration string \"{}\"; must be of the form {}",
            value, DURATION_REGEX
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_duration() {
        assert_eq!(parse_duration("1h"), Ok(Duration::from_secs(3600)));
        assert_eq!(parse_duration("3m"), Ok(Duration::from_secs(180)));
        assert_eq!(parse_duration("10s"), Ok(Duration::from_secs(10)));
        assert_eq!(parse_duration("100ms"), Ok(Duration::from_millis(100)));
    }

    #[test]
    fn test_parse_duration_err() {
        assert!(parse_duration("100").is_err());
        assert!(parse_duration("10 0").is_err());
        assert!(parse_duration("foobar").is_err());
    }
}
