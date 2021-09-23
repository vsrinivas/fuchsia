// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    chrono::{DateTime, Local},
    chrono_english::{parse_date_string, Dialect},
    diagnostics_data::Severity,
    ffx_core::ffx_command,
    std::time::Duration,
};

#[derive(Clone, Debug, PartialEq)]
pub enum TimeFormat {
    Utc,
    Local,
    Monotonic,
}

impl std::str::FromStr for TimeFormat {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let lower = s.to_ascii_lowercase();
        match lower.as_str() {
            "local" => Ok(TimeFormat::Local),
            "utc" => Ok(TimeFormat::Utc),
            "monotonic" => Ok(TimeFormat::Monotonic),
            _ => Err(format!(
                "'{}' is not a valid value: must be one of 'local', 'utc', 'monotonic'",
                s
            )),
        }
    }
}

#[ffx_command()]
#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "log",
    description = "Display logs from a target device",
    note = "\
The `--moniker` argument expects a component selector. See this page for
documentation:
https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#ComponentSelector.

You may find the `component select moniker` command useful for exploring the topology
and identifying the selector that matches the component(s) of interest.",
    example = "\
Dump the most recent logs and stream new ones as they happen:
  $ ffx log

Stream new logs starting from the current time, filtering for severity of at least \"WARN\":
  $ ffx log --severity warn --from-now

Dump all logs from components with a moniker, url, or message containing \"remote-control\" logged
in the last 5 minutes:
  $ ffx log --filter remote-control --dump --since \"5m ago\"

Dump all logs from the last 30 minutes logged before 5 minutes ago:
  $ ffx log --dump --since \"30m ago\" --until \"5m ago\"

Stream logs from components with moniker, url or message that do not include \"sys\":
  $ ffx log --exclude sys

Dump ERROR logs with moniker, url or message containing either \"netstack\" or \"remote-control.cm\",
but which do not contain \"sys\":
  $ ffx log --severity error --filter netstack --filter remote-control.cm --exclude sys --dump"
)]
pub struct LogCommand {
    /// filter for a string in either the message, component or url.
    /// May be repeated.
    #[argh(option)]
    pub filter: Vec<String>,

    /// exclude a string in either the message, component or url.
    /// May be repeated.
    #[argh(option)]
    pub exclude: Vec<String>,

    /// filter for only logs with a given tag. May be repeated.
    #[argh(option)]
    pub tags: Vec<String>,

    /// exclude logs with a given tag. May be repeated.
    #[argh(option)]
    pub exclude_tags: Vec<String>,

    /// set the minimum severity
    #[argh(option, default = "Severity::Info")]
    pub severity: Severity,

    /// outputs only kernel logs.
    #[argh(switch)]
    pub kernel: bool,

    /// when --dump is not provided, start printing logs only from the moment
    /// the command is run
    #[argh(switch)]
    pub from_now: bool,

    /// dump a snapshot of logs instead of streaming new ones
    #[argh(switch)]
    pub dump: bool,

    /// show only logs after a certain time
    /// valid only with --dump
    #[argh(option, from_str_fn(parse_time))]
    pub since: Option<DateTime<Local>>,

    /// show only logs after a certain time (as a monotonic
    /// timestamp in seconds from the target).
    /// valid only with --dump
    #[argh(option, from_str_fn(parse_duration))]
    pub since_monotonic: Option<Duration>,

    /// show only logs until a certain time
    /// valid only with --dump
    #[argh(option, from_str_fn(parse_time))]
    pub until: Option<DateTime<Local>>,

    /// show only logs until a certain time (as a monotonic
    /// timestamp in seconds from the target).
    /// valid only with --dump
    #[argh(option, from_str_fn(parse_duration))]
    pub until_monotonic: Option<Duration>,

    /// hide the tag field from output (does not exclude any log messages)
    #[argh(switch)]
    pub hide_tags: bool,

    /// disable coloring logs according to severity.
    /// Note that you can permanently disable this with
    /// `ffx config set log_cmd.color false`
    #[argh(switch)]
    pub no_color: bool,

    /// shows process-id and thread-id in log output
    #[argh(switch)]
    pub show_metadata: bool,

    /// how to display log timestamps.
    /// Options are "utc", "local", or "monotonic" (i.e. nanos since target boot).
    /// Default is monotonic.
    #[argh(option, default = "TimeFormat::Monotonic")]
    pub clock: TimeFormat,

    /// if provided, logs will not be symbolized
    #[argh(switch)]
    pub no_symbols: bool,
}

pub fn parse_time(value: &str) -> Result<DateTime<Local>, String> {
    let d = parse_date_string(value, Local::now(), Dialect::Us)
        .map_err(|e| format!("invalid date string: {}", e));
    d
}

pub fn parse_duration(value: &str) -> Result<Duration, String> {
    Ok(Duration::from_secs(
        value.parse().map_err(|e| format!("value '{}' is not a number: {}", value, e))?,
    ))
}
