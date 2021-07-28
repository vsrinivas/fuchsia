// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{FromArgs, TopLevelCommand},
    ffx_config::FfxConfigBacked,
    ffx_core::ffx_command,
    ffx_lib_sub_command::Subcommand,
};

#[ffx_command()]
#[derive(FfxConfigBacked, FromArgs, Debug, PartialEq)]
/// Fuchsia's developer tool
pub struct Ffx {
    #[argh(option, short = 'c')]
    /// override default configuration
    pub config: Vec<String>,

    #[argh(option, short = 'e')]
    /// override default environment settings
    pub env: Option<String>,

    #[argh(option, short = 't')]
    #[ffx_config_default("target.default")]
    /// apply operations across single or multiple targets
    pub target: Option<String>,

    #[argh(option, short = 'T')]
    #[ffx_config_default(key = "proxy.timeout_secs", default = "1.0")]
    /// override default proxy timeout
    pub timeout: Option<f64>,

    #[argh(subcommand)]
    pub subcommand: Option<Subcommand>,
}

/// Extract the base cmd from a path
fn cmd<'a>(default: &'a String, path: &'a String) -> &'a str {
    std::path::Path::new(path).file_name().map(|s| s.to_str()).flatten().unwrap_or(default.as_str())
}

/// Create a `FromArgs` type from the current process's `env::args`.
///
/// This function will exit early from the current process if argument parsing
/// was unsuccessful or if information like `--help` was requested.
pub fn from_env<T: TopLevelCommand>() -> T {
    let strings: Vec<String> = std::env::args().collect();
    let cmd = cmd(&strings[0], &strings[0]);
    let strs: Vec<&str> = strings.iter().map(|s| s.as_str()).collect();
    T::from_args(&[cmd], &strs[1..]).unwrap_or_else(|early_exit| {
        println!("{}", early_exit.output);
        println!("See 'ffx help <command>' for more information on a specific command.");
        std::process::exit(match early_exit.status {
            Ok(()) => 0,
            Err(()) => 1,
        })
    })
}

/// Create a string of the current process's `env::args` that replaces user-supplied parameter values with the parameter name to enable safe analytics data collection.
///
/// This function will exit early from the current process if argument parsing
/// was unsuccessful or if information like `--help` was requested.
pub fn redact_arg_values<T: TopLevelCommand>() -> String {
    let strings: Vec<String> = std::env::args().collect();
    let cmd = cmd(&strings[0], &strings[0]);
    let strs: Vec<&str> = strings.iter().map(|s| s.as_str()).collect();
    let x = T::redact_arg_values(&[cmd], &strs[1..]);
    match x {
        Ok(s) => s[1..].join(" "),
        Err(e) => e.output,
    }
}
