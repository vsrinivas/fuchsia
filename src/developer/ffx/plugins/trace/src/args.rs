// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
/// Interact with the tracing subsystem
#[argh(subcommand, name = "trace")]
pub struct TraceCommand {
    #[argh(subcommand)]
    pub sub_cmd: TraceSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum TraceSubCommand {
    ListCategories(ListCategories),
    ListProviders(ListProviders),
    Start(Start),
    Stop(Stop),
    // More commands including `record` and `convert` to follow.
}

#[derive(FromArgs, PartialEq, Debug)]
/// List the target's known trace categories
#[argh(subcommand, name = "list-categories")]
pub struct ListCategories {}

#[derive(FromArgs, PartialEq, Debug)]
/// List the target's trace providers
#[argh(subcommand, name = "list-providers")]
pub struct ListProviders {}

// Work around argh's handling of Vec.  Listing categories as a comma
// separated list of values rather than a repeated keyed option
// is much more concise when dealing with a large set of categories.
pub type TraceCategories = Vec<String>;

// This list should be kept in sync with DEFAULT_CATEGORIES in
// //src/testing/sl4f/src/tracing/facade.rs as well as the help text below
pub const DEFAULT_CATEGORIES: &[&'static str] = &[
    "app",
    "audio",
    "benchmark",
    "blobfs",
    "gfx",
    "input",
    "kernel:meta",
    "kernel:sched",
    "ledger",
    "magma",
    "minfs",
    "modular",
    "view",
    "flutter",
    "dart",
    "dart:compiler",
    "dart:dart",
    "dart:debugger",
    "dart:embedder",
    "dart:gc",
    "dart:isolate",
    "dart:profiler",
    "dart:vm",
];

#[derive(FromArgs, PartialEq, Debug)]
/// Record a trace
#[argh(subcommand, name = "stop")]
pub struct Stop {
    /// name of output trace file.  Defaults to trace.fxt.
    #[argh(option, default = "String::from(\"trace.fxt\")")]
    pub output: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Record a trace
#[argh(subcommand, name = "start")]
pub struct Start {
    /// size of per-provider trace buffer in MB.  Defaults to 4.
    #[argh(option, default = "4")]
    pub buffer_size: u32,

    /// comma-separated list of categories to enable.  Defaults
    /// to "app,audio,benchmark,blobfs,gfx,input,kernel:meta,
    /// kernel:sched,ledger,magma,minfs,modular,view,flutter,
    /// dart,dart:compiler,dart:dart,dart:debugger,dart:embedder,
    /// dart:gc,dart:isolate,dart:profiler,dart:vm"
    #[argh(
        option,
        default = "DEFAULT_CATEGORIES.into_iter().cloned().map(String::from).collect()",
        from_str_fn(parse_categories)
    )]
    pub categories: TraceCategories,

    /// duration of trace capture in seconds.
    #[argh(option)]
    pub duration: Option<f64>,

    /// name of output trace file.  Defaults to trace.fxt.
    #[argh(option, default = "String::from(\"trace.fxt\")")]
    pub output: String,

    /// whether to run the trace in the background. Defaults to false,
    /// which means the trace will run in "interactive" mode.
    #[argh(switch)]
    pub background: bool,
}

fn parse_categories(value: &str) -> Result<TraceCategories, String> {
    let mut cats = Vec::new();

    if value.is_empty() {
        return Err("no categories specified".to_string());
    }

    for cat in value.split(",") {
        if cat.is_empty() {
            return Err("empty category specified".to_string());
        }
        cats.push(String::from(cat));
    }

    Ok(cats)
}

#[cfg(test)]
mod tests {
    use super::*;
    const START_CMD_NAME: &'static [&'static str] = &["start"];

    #[test]
    fn test_parse_categories() {
        assert_eq!(parse_categories(&"a"), Ok(vec!["a".to_string()]));

        assert_eq!(
            parse_categories(&"a,b,c:d"),
            Ok(vec!["a".to_string(), "b".to_string(), "c:d".to_string()])
        );

        assert_eq!(parse_categories(&""), Err("no categories specified".to_string()));
        assert_eq!(parse_categories(&"a,,b"), Err("empty category specified".to_string()));
    }

    #[test]
    fn test_default_categories() {
        // This tests the code in a string that is passed as a default to argh. It is compile
        // checked because of the generated code, but this ensures that it is functionally correct.
        let cmd = Start::from_args(START_CMD_NAME, &[]).unwrap();
        assert_eq!(cmd.categories.len(), DEFAULT_CATEGORIES.len());
        for category in DEFAULT_CATEGORIES {
            assert!(cmd.categories.iter().any(|c| c == category));
        }
    }
}
