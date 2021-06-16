// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    chrono::{DateTime, Local},
    chrono_english::{parse_date_string, Dialect},
    diagnostics_data::Severity,
    ffx_core::ffx_command,
    fidl_fuchsia_diagnostics::ComponentSelector,
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
Filters must be provided to the top-level `target log` command,
*not* to the sub-command (see examples above)

The `--moniker` argument expects a component selector. See this page for
documentation:
https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#ComponentSelector.

You may find the `component select` command useful for exploring the topology
and identifying the selector that matches the component(s) of interest.",
    example = "\
Dump the most recent logs and stream new ones as they happen:
  $ ffx target log watch

Stream new logs without dumping recent ones, filtering for severity of at least \"WARN\":
  $ ffx target log --min-severity warn watch --dump false

Dump all logs from components with a moniker, url, or message containing \"remote-control\":
  $ ffx target log --filter remote-control dump

Stream logs from components with moniker, url or message that do not include \"sys\":
  $ ffx target log --exclude sys watch

Dump ERROR logs with moniker, url or message containing either \"klog\" or \"remote-control.cm\",
but which do not contain \"sys\":
  $ ffx target log --min-severity error --filter klog --filter remote-control.cm --exclude sys dump

Dump logs with monikers matching component selectors, instead of text matches:
  $ ffx target log --moniker \"core/remote-*\" --exclude-moniker \"sys/*\" dump"
)]
pub struct LogCommand {
    #[argh(subcommand)]
    pub cmd: LogSubCommand,
    /// filter for a string in either the message, component or url.
    #[argh(option)]
    pub filter: Vec<String>,
    /// exclude a string in either the message, component or url.
    #[argh(option)]
    pub exclude: Vec<String>,

    /// toggle coloring logs according to severity
    #[argh(option)]
    pub color: Option<bool>,

    /// ignore any failure to find the symbolizer binary.
    #[argh(switch)]
    pub ignore_symbolizer_failure: bool,

    /// how to display log timestamps
    #[argh(option, default = "TimeFormat::Monotonic")]
    pub time: TimeFormat,

    /// allowed monikers
    #[argh(
        option,
        from_str_fn(parse_component_selector),
        description = "filter log entries using component selectors"
    )]
    pub moniker: Vec<ComponentSelector>,
    /// disallowed monikers
    #[argh(
        option,
        from_str_fn(parse_component_selector),
        description = "exclude log entries matching a component selector"
    )]
    pub exclude_moniker: Vec<ComponentSelector>,
    /// set the minimum severity
    #[argh(option, default = "Severity::Info")]
    pub min_severity: Severity,
}

fn parse_component_selector(value: &str) -> Result<ComponentSelector, String> {
    selectors::parse_component_selector(&value.to_string())
        .map_err(|e| format!("failed to parse the moniker filter: {}", e))
}

fn parse_time(value: &str) -> Result<DateTime<Local>, String> {
    let d = parse_date_string(value, Local::now(), Dialect::Us)
        .map_err(|e| format!("invalid date string: {}", e));
    d
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
#[argh(subcommand)]
pub enum LogSubCommand {
    Watch(WatchCommand),
    Dump(DumpCommand),
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Watches for and prints logs from a target. Optionally dumps recent logs first.
#[argh(subcommand, name = "watch")]
pub struct WatchCommand {
    #[argh(option, default = "true")]
    /// if true, dumps recent logs before printing new ones.
    pub dump: bool,
}
#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Dumps all logs from a target.
#[argh(subcommand, name = "dump")]
pub struct DumpCommand {
    /// show only logs after a certain time
    #[argh(option, from_str_fn(parse_time))]
    pub from: Option<DateTime<Local>>,

    /// show only logs after a certain time (as a monotonic
    /// timestamp in seconds from the target).
    #[argh(option, from_str_fn(parse_duration))]
    pub from_monotonic: Option<Duration>,

    /// show only logs until a certain time
    #[argh(option, from_str_fn(parse_time))]
    pub to: Option<DateTime<Local>>,

    /// show only logs until a certain time (as a monotonic
    /// timestamp in seconds from the target).
    #[argh(option, from_str_fn(parse_duration))]
    pub to_monotonic: Option<Duration>,
}

fn parse_duration(value: &str) -> Result<Duration, String> {
    Ok(Duration::from_secs(
        value.parse().map_err(|e| format!("value '{}' is not a number: {}", value, e))?,
    ))
}
#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_parse_component_selector() {
        assert!(parse_component_selector("core/*remote*").is_ok());
    }

    #[test]
    fn test_parse_invalid_component_selector() {
        assert!(parse_component_selector("").is_err());
    }
}
