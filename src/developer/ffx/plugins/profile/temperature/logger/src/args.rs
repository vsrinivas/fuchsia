// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, std::time::Duration};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "logger",
    description = "Controls the temperature-logger component",
    example = "\
To log temperatures every 100ms for 30 seconds:

    $ ffx profile temperature logger start --interval 100ms --duration 30s

To log temperatures every 1s indefinitely:

    $ ffx profile temperature logger start --interval 1s",
    note = "\
Logged temperatures are not output by this command directly; rather, they will be available via
iquery under core/temperature-logger and via tracing in the temperature_logger category.

If the temperature-logger component is not available to the target, then this command will not work
properly."
)]
/// Top-level command.
pub struct Command {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum Subcommand {
    Start(StartCommand),
    Stop(StopCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Start logging on the target
#[argh(subcommand, name = "start")]
pub struct StartCommand {
    #[argh(option, long = "interval", short = 'i', from_str_fn(parse_duration))]
    /// interval between temperature samples
    pub interval: Duration,

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
        assert!(parse_duration("100").is_err());
    }
}
