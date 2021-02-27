// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs, diagnostics_data::Severity, ffx_core::ffx_command,
    fidl_fuchsia_diagnostics::ComponentSelector,
};

#[ffx_command()]
#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "log",
    description = "",
    note = "\
Display logs from a target device

Note that filters must be provided to the top-level `target log` command,
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

Dump all logs from components matching a moniker:
  $ ffx target log --moniker \"core/remote-control\" dump

Stream logs from components matching a wildcarded moniker or from klog:
  $ ffx target log --moniker \"core/remote*\" --moniker \"klog\" watch

Dump logs containing the word \"fuchsia\"
  $ ffx target log --msg-contains fuchsia dump"
)]
pub struct LogCommand {
    #[argh(subcommand)]
    pub cmd: LogSubCommand,

    /// toggle coloring logs according to severity
    #[argh(option)]
    pub color: Option<bool>,
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
    /// filter for words in the message body
    #[argh(option)]
    pub msg_contains: Vec<String>,
    /// set the minimum severity
    #[argh(option, default = "Severity::Info")]
    pub min_severity: Severity,
}

fn parse_component_selector(value: &str) -> Result<ComponentSelector, String> {
    selectors::parse_component_selector(&value.to_string())
        .map_err(|e| format!("failed to parse the moniker filter: {}", e))
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
pub struct DumpCommand {}

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
