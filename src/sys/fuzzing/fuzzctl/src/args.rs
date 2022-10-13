// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, fidl_fuchsia_fuzzer as fuzz, fuchsia_fuzzctl::constants::*, url::Url};

/// Interacts with the fuzz-manager.
#[derive(Clone, Debug, FromArgs, PartialEq)]
pub struct FuzzCtlCommand {
    /// command to execute
    #[argh(subcommand)]
    pub command: FuzzCtlSubcommand,
}

/// Individual subcommands that can be run from the command line.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum FuzzCtlSubcommand {
    Reset(ResetSubcommand),
    RunLibFuzzer(RunLibFuzzerSubcommand),
}

/// Command to reset a fuzzer to an initial state.
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "reset")]
pub struct ResetSubcommand {
    /// fuzzer component URL
    #[argh(positional, from_str_fn(parse_url))]
    pub url: Url,
}

/// Command to run a fuzzing task using libFuzzer.
///
/// The driving goal for `fuzz_ctl`'s argument parsing is to provide an interface as close to what
/// libFuzzer provides and ClusterFuzz expects as possible. As a result, in addition to the normal
/// argh-style options, libFuzzer-style options are also supported, e.g. `--my-key val` is
/// equivalent to `-my_key=val`. See `FuzzCtl::run` for this translation.
///
/// Several other argument design choices are also made for ClusterFuzz compatibility, including:
///
///   * Switches like `--detect-leaks` would typically be an `#[argh(switch)]` with type `bool`, but
///     each is instead an `#[argh(option)]` with type `u64`. This more closely matches libFuzzer,
///     which wants to specify these as `-detect_leaks=0` or `-detect_leaks=1`.
///
///   * Some numeric options, namely times and sizes in bytes or megabytes, are `f64`s instead of
///     `u64`s. This is done to support ClusterFuzz, which has the unfortunate habit of passing args
///     like `-max_total_time=2700.0`.
///
///   * Some options are present but are otherwise ignored to allow ClusterFuzz to specify them
///     without causing an error.
///
#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "run_libfuzzer")]
pub struct RunLibFuzzerSubcommand {
    /// directory used to store fuzzer artifacts
    #[argh(option)]
    pub artifact_prefix: Option<String>,

    /// try to detect memory leaks
    #[argh(option, from_str_fn(parse_switch))]
    detect_leaks: Option<bool>,

    /// currently ignored
    #[argh(option)]
    dict: Option<String>,

    /// save the artifact this location if one is produced
    #[argh(option)]
    pub exact_artifact_path: Option<String>,

    /// currently ignored
    #[argh(option)]
    jobs: Option<u64>,

    /// maximum amount of memory, in megabytes, that the fuzzer is allowed to allocate
    #[argh(option)]
    malloc_limit_mb: Option<f64>,

    /// maximum size of fuzzer-generated inputs, in bytes
    #[argh(option)]
    max_len: Option<f64>,

    /// if nonzero, stop the fuzzer after this many seconds
    #[argh(option)]
    max_total_time: Option<f64>,

    /// perform corpus merging instead of fuzzing
    #[argh(option, from_str_fn(parse_switch), default = "false")]
    pub merge: bool,

    /// currently ignored
    #[argh(option)]
    merge_control_file: Option<String>,

    /// perform input minimization instead of fuzzing
    #[argh(option, from_str_fn(parse_switch), default = "false")]
    pub minimize_crash: bool,

    /// maximum number of consecutive mutations to apply to an input
    #[argh(option)]
    mutate_depth: Option<u16>,

    /// display fuzzing stats when task ends
    #[argh(option, from_str_fn(parse_switch))]
    print_final_stats: Option<bool>,

    /// purge the sanitizer's allocator quarantine each time this many seconds elapse
    #[argh(option)]
    purge_allocator_interval: Option<f64>,

    /// trigger an error if the fuzzer uses more than this much memory
    #[argh(option)]
    rss_limit_mb: Option<f64>,

    /// if nonzero, stop the fuzzer after this many runs
    #[argh(option)]
    runs: Option<u32>,

    /// seeds the PRNG
    #[argh(option)]
    seed: Option<u32>,

    /// maximum amount of time, in second that the fuzzer is allowed to spend on any one input
    #[argh(option)]
    timeout: Option<f64>,

    /// fuzzer component URL
    #[argh(positional, from_str_fn(parse_url))]
    pub url: Url,

    /// use data flow traces as part of the fuzzing coverage data
    #[argh(option, from_str_fn(parse_switch))]
    use_value_profile: Option<bool>,

    /// files to test or directories to fuzz from, but not both, prefixed with 'tmp/'
    #[argh(positional)]
    pub data: Vec<String>,
}

fn parse_switch(value: &str) -> Result<bool, String> {
    let n: u64 = value.parse().map_err(|e| format!("value '{}' is not a number: {}", value, e))?;
    Ok(n != 0)
}

fn parse_url(value: &str) -> Result<Url, String> {
    Url::parse(value).map_err(|e| format!("'{}' is not an valid URL: {}", value, e))
}

impl RunLibFuzzerSubcommand {
    pub fn get_options(&self) -> fuzz::Options {
        fuzz::Options {
            runs: self.runs,
            max_total_time: self.max_total_time.map(|s| (s as i64) * NANOS_PER_SECOND),
            seed: self.seed,
            max_input_size: self.max_len.map(|f| f as u64),
            mutation_depth: self.mutate_depth,
            detect_leaks: self.detect_leaks,
            run_limit: self.timeout.map(|s| (s as i64) * NANOS_PER_SECOND),
            malloc_limit: self.malloc_limit_mb.map(|mb| (mb as u64) * BYTES_PER_MB),
            oom_limit: self.rss_limit_mb.map(|mb| (mb as u64) * BYTES_PER_MB),
            purge_interval: self.purge_allocator_interval.map(|s| (s as i64) * NANOS_PER_SECOND),
            print_final_stats: self.print_final_stats,
            use_value_profile: self.use_value_profile,
            ..fuzz::Options::EMPTY
        }
    }
}
