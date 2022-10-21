// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    chrono::{DateTime, Local},
    chrono_english::{parse_date_string, Dialect},
    diagnostics_data::Severity,
    ffx_core::ffx_command,
    fidl_fuchsia_developer_ffx::SessionSpec,
    fidl_fuchsia_diagnostics::{Interest, LogInterestSelector, Severity as FidlSeverity},
    selectors::{self, VerboseError},
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
    note = "Logs are proactively retrieved from target devices and cached on the host.

Symbolization is performed in the background using the symbolizer host tool. You can pass
additional arguments to the symbolizer tool (for example, to add a remote symbol server) using:
  $ ffx config set proactive_log.symbolize.extra_args \"--symbol-server gs://some-url/path --symbol-server gs://some-other-url/path ...\"

To learn more about configuring the log viewer, visit https://fuchsia.dev/fuchsia-src/development/tools/ffx/commands/log",
    example = "\
Dump the most recent logs and stream new ones as they happen:
  $ ffx log

Stream new logs starting from the current time, filtering for severity of at least \"WARN\":
  $ ffx log --severity warn --since now

Stream logs where the source moniker, component url and message do not include \"sys\":
  $ ffx log --exclude sys

Stream ERROR logs with source moniker, component url or message containing either
\"netstack\" or \"remote-control.cm\", but not containing \"sys\":
  $ ffx log --severity error --filter netstack --filter remote-control.cm --exclude sys

Dump all available logs where the source moniker, component url, or message contains
\"remote-control\":
  $ ffx log --filter remote-control dump

Dump all logs from the last 30 minutes logged before 5 minutes ago:
  $ ffx log --since \"30m ago\" --until \"5m ago\" dump

Enable DEBUG logs from the \"core/audio\" component while logs are streaming:
  $ ffx log --select core/audio#DEBUG"
)]
pub struct LogCommand {
    #[argh(subcommand)]
    pub sub_command: Option<LogSubCommand>,

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

    /// set the minimum severity. Accepted values (from lower to higher) are: trace, debug, info,
    /// warn (or warning), error, fatal. This field is case insensitive.
    #[argh(option, default = "Severity::Info")]
    pub severity: Severity,

    /// outputs only kernel logs.
    #[argh(switch)]
    pub kernel: bool,

    /// show only logs after a certain time
    #[argh(option, from_str_fn(parse_time))]
    pub since: Option<DateTime<Local>>,

    /// show only logs after a certain time (as a monotonic
    /// timestamp: seconds from the target's boot time).
    #[argh(option, from_str_fn(parse_duration))]
    pub since_monotonic: Option<Duration>,

    /// show only logs until a certain time
    #[argh(option, from_str_fn(parse_time))]
    pub until: Option<DateTime<Local>>,

    /// show only logs until a certain time (as a monotonic
    /// timestamp: seconds since the target's boot time).
    #[argh(option, from_str_fn(parse_duration))]
    pub until_monotonic: Option<Duration>,

    /// hide the tag field from output (does not exclude any log messages)
    #[argh(switch)]
    pub hide_tags: bool,

    /// hide the file and line number field from output (does not exclude any log messages)
    #[argh(switch)]
    pub hide_file: bool,

    /// disable coloring logs according to severity.
    /// Note that you can permanently disable this with
    /// `ffx config set log_cmd.color false`
    #[argh(switch)]
    pub no_color: bool,

    /// shows process-id and thread-id in log output
    #[argh(switch)]
    pub show_metadata: bool,

    /// shows the full moniker in log output. By default this is false and only the last segment
    /// of the moniker is printed.
    #[argh(switch)]
    pub show_full_moniker: bool,

    /// how to display log timestamps.
    /// Options are "utc", "local", or "monotonic" (i.e. nanos since target boot).
    /// Default is monotonic.
    #[argh(option, default = "TimeFormat::Monotonic")]
    pub clock: TimeFormat,

    /// if provided, logs will not be symbolized
    #[argh(switch)]
    pub no_symbols: bool,

    /// configure the log settings on the target device for components matching
    /// the given selector. This modifies the minimum log severity level emitted
    /// by components during the logging session.
    /// Specify using the format <component-selector>#<log-level>, with level
    /// as one of FATAL|ERROR|WARN|INFO|DEBUG|TRACE.
    /// May be repeated.
    #[argh(option, from_str_fn(parse_log_interest_selector))]
    pub select: Vec<LogInterestSelector>,

    /// if provided, overrides the default log spam list path that's optionally
    /// specified in FFX config under the "log_cmd.spam_filepath" key.
    #[argh(option)]
    pub spam_list_path: Option<String>,

    /// if set, disable log spam filtering
    #[argh(switch)]
    pub disable_spam_filter: bool,

    /// if set and spam filter is enabled, spams will be displayed and highlighted
    #[argh(switch)]
    pub enable_spam_highlight: bool,
}

impl Default for LogCommand {
    fn default() -> Self {
        LogCommand {
            filter: vec![],
            exclude: vec![],
            tags: vec![],
            exclude_tags: vec![],
            hide_tags: false,
            hide_file: false,
            clock: TimeFormat::Monotonic,
            no_color: false,
            kernel: false,
            severity: Severity::Info,
            show_metadata: false,
            no_symbols: false,
            since: None,
            since_monotonic: None,
            until: None,
            until_monotonic: None,
            sub_command: None,
            select: vec![],
            show_full_moniker: false,
            spam_list_path: None,
            disable_spam_filter: false,
            enable_spam_highlight: false,
        }
    }
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
#[argh(subcommand)]
pub enum LogSubCommand {
    Watch(WatchCommand),
    Dump(DumpCommand),
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Watches for and prints logs from a target. Default if no sub-command is specified.
#[argh(subcommand, name = "watch")]
pub struct WatchCommand {}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Dumps all log from a given target's session.
#[argh(subcommand, name = "dump")]
pub struct DumpCommand {
    /// A specifier indicating which session you'd like to retrieve logs for.
    /// For example, providing ~1 retrieves the most-recent session,
    /// ~2 the second-most-recent, and so on.
    /// Defaults to the most recent session.
    #[argh(positional, default = "SessionSpec::Relative(0)", from_str_fn(parse_session_spec))]
    pub session: SessionSpec,
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

pub fn parse_session_spec(value: &str) -> Result<SessionSpec, String> {
    if value.is_empty() {
        return Err(String::from("session identifier cannot be empty"));
    }

    if value == "0" {
        return Err(String::from("'0' is not a valid session specifier: use ~1 for the most recent session in `dump` mode."));
    }

    let split = value.split_once('~');
    if let Some((_, val)) = split {
        Ok(SessionSpec::Relative(val.parse().map_err(|e| {
            format!(
                "previous session provided with '~' but could not parse the rest as a number: {}",
                e
            )
        })?))
    } else {
        Ok(SessionSpec::TimestampNanos(value.parse().map_err(|e| {
            format!("session identifier was provided, but could not be parsed as a number: {}", e)
        })?))
    }
}

pub fn parse_log_interest_selector(selector: &str) -> Result<LogInterestSelector, String> {
    let invalid_selector = format!("Invalid component interest selector: '{}'.", selector);
    let mut parts = selector.split('#');

    // Split each arg into sub string vectors containing strings
    // for component [0] and interest [1] respectively.
    let component = parts.next().ok_or_else(|| invalid_selector.clone())?;
    let interest = parts.next().ok_or_else(|| invalid_selector.clone())?;
    if let Some(_extra) = parts.next() {
        return Err(invalid_selector.clone());
    }
    let selector = match selectors::parse_component_selector::<VerboseError>(component) {
        Ok(s) => s,
        Err(e) => return Err(format!("{} Error: {}", invalid_selector.clone(), e)),
    };

    let min_severity = match interest.to_uppercase().as_ref() {
        "TRACE" => Some(FidlSeverity::Trace),
        "DEBUG" => Some(FidlSeverity::Debug),
        "INFO" => Some(FidlSeverity::Info),
        "WARN" => Some(FidlSeverity::Warn),
        "ERROR" => Some(FidlSeverity::Error),
        "FATAL" => Some(FidlSeverity::Fatal),
        "" => None,
        _ => return Err(invalid_selector),
    };
    Ok(LogInterestSelector { selector, interest: Interest { min_severity, ..Interest::EMPTY } })
}

#[cfg(test)]
mod test {
    use {super::*, selectors::parse_component_selector};

    #[test]
    fn test_session_spec_non_zero() {
        assert_eq!(parse_session_spec("~1").unwrap(), SessionSpec::Relative(1));
        assert_eq!(parse_session_spec("~15").unwrap(), SessionSpec::Relative(15));
    }

    #[test]
    fn test_session_spec_absolute() {
        assert_eq!(parse_session_spec("1234567").unwrap(), SessionSpec::TimestampNanos(1234567));
    }

    #[test]
    fn test_session_spec_error() {
        assert!(parse_session_spec("~abc").is_err());
        assert!(parse_session_spec("abc").is_err());
    }

    #[test]
    fn test_log_interest_selector() {
        assert_eq!(
            parse_log_interest_selector("core/network#FATAL").unwrap(),
            LogInterestSelector {
                selector: parse_component_selector::<VerboseError>("core/network").unwrap(),
                interest: Interest { min_severity: Some(FidlSeverity::Fatal), ..Interest::EMPTY }
            }
        );
        assert_eq!(
            parse_log_interest_selector("any/component#INFO").unwrap(),
            LogInterestSelector {
                selector: parse_component_selector::<VerboseError>("any/component").unwrap(),
                interest: Interest { min_severity: Some(FidlSeverity::Info), ..Interest::EMPTY }
            }
        );
    }
    #[test]
    fn test_log_interest_selector_error() {
        assert!(parse_log_interest_selector("anything////#FATAL").is_err());
        assert!(parse_log_interest_selector("core/network").is_err());
        assert!(parse_log_interest_selector("core/network#FAKE").is_err());
    }
}
