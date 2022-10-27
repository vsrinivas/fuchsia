// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ToolSuite;
use anyhow::{Context, Result};
use argh::EarlyExit;
use argh::FromArgs;
use errors::ffx_error;
use ffx_config::EnvironmentContext;
use ffx_config::FfxConfigBacked;
use ffx_daemon_proxy::Injection;
use ffx_target::TargetKind;
use ffx_writer::Format;
use hoist::Hoist;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::time::Duration;

pub use ffx_daemon_proxy::DaemonVersionCheck;

/// The environment variable name used for overriding the command name in help
/// output.
const FFX_WRAPPER_INVOKE: &'static str = "FFX_WRAPPER_INVOKE";

#[derive(Clone, Debug, PartialEq)]
/// The relevant argument and environment variables necessary to parse or
/// reconstruct an ffx command invocation.
pub struct FfxCommandLine {
    pub command: Vec<String>,
    pub args: Vec<String>,
}

impl FfxCommandLine {
    /// Construct the command from the system environment ([`std::env::args`] and [`std::env::var`]), using
    /// the FFX_WRAPPER_INVOKE environment variable to obtain the `wrapper_name`, if present. See [`FfxCommand::new`]
    /// for more information.
    pub fn from_env() -> Result<Self> {
        let argv = Vec::from_iter(std::env::args());
        let wrapper_name = std::env::var(FFX_WRAPPER_INVOKE).ok();
        Self::new(wrapper_name, argv)
    }

    /// Extract the command name from the given argument list, allowing for an overridden command name
    /// from a wrapper invocation so we provide useful information to the user. If the override has spaces, it will
    /// be split into multiple commands.
    ///
    /// Returns a tuple of the command and the remaining arguments
    pub fn new(
        wrapper_name: Option<String>,
        argv: impl IntoIterator<Item = String>,
    ) -> Result<Self> {
        let mut args = argv.into_iter();
        let arg0 = args.next().context("No first argument in argument vector")?;
        let args = Vec::from_iter(args);
        let command = wrapper_name.map_or_else(
            || vec![Self::base_cmd(&arg0)],
            |s| s.split(" ").map(str::to_owned).collect(),
        );
        Ok(Self { command, args })
    }

    /// Parse the command line arguments given into an Ffx argument struct, returning the
    /// EarlyExit error if the parse failed
    pub fn try_parse<T: ToolSuite>(&self) -> Result<Ffx, EarlyExit> {
        Ffx::from_args(&Vec::from_iter(self.cmd_iter()), &Vec::from_iter(self.args_iter())).map_err(
            |early_exit| {
                let output = early_exit.output
                    + &format!(
                        "\nBuilt-in Commands:{subcommands}\n\nNote: There may be more commands available, use `{cmd} commands` for a complete list.\nSee '{cmd} <command> help' for more information on a specific command.",
                        subcommands = argh::print_subcommands(T::global_command_list().iter().copied()),
                        cmd = self.command.join(" "),
                    );
                EarlyExit { output, ..early_exit }
            },
        )
    }

    /// Parse the command line arguments given into an Ffx argument struct, exiting with the
    /// appropriate error code if there's an error parsing.
    ///
    /// This function will exit early from the current process if argument parsing
    /// was unsuccessful or if information like `--help` was requested.
    pub fn parse<T: ToolSuite>(&self) -> Ffx {
        self.try_parse::<T>().unwrap_or_else(|early_exit| {
            println!("{}", early_exit.output);

            std::process::exit(match early_exit.status {
                Ok(()) => 0,
                Err(()) => 1,
            })
        })
    }

    /// Create a string of the current process's `env::args` that replaces user-supplied parameter
    /// values with the parameter name to enable safe analytics data collection.
    ///
    /// This function will exit early from the current process if argument parsing
    /// was unsuccessful or if information like `--help` was requested.
    pub fn redact_arg_values(&self) -> String {
        let x = Ffx::redact_arg_values(
            &Vec::from_iter(self.cmd_iter()),
            &Vec::from_iter(self.args_iter()),
        );
        match x {
            Ok(s) => s[1..].join(" "),
            Err(e) => e.output,
        }
    }

    /// Returns an iterator of the command part of the command line
    pub fn cmd_iter<'a>(&'a self) -> impl Iterator<Item = &'a str> {
        self.command.iter().map(|s| s.as_str())
    }

    /// Returns an iterator of the command part of the command line
    pub fn args_iter<'a>(&'a self) -> impl Iterator<Item = &'a str> {
        self.args.iter().map(|s| s.as_str())
    }

    /// Returns an iterator of the whole command line
    pub fn all_iter<'a>(&'a self) -> impl Iterator<Item = &'a str> {
        self.cmd_iter().chain(self.args_iter())
    }

    /// Extract the base cmd from a path
    fn base_cmd(path: &str) -> String {
        std::path::Path::new(path)
            .file_name()
            .map(|s| s.to_str())
            .flatten()
            .unwrap_or(path)
            .to_owned()
    }
}

#[derive(Clone, FfxConfigBacked, FromArgs, Debug, PartialEq)]
/// Fuchsia's developer tool
pub struct Ffx {
    #[argh(option, short = 'c')]
    /// override configuration values (key=value or json)
    pub config: Vec<String>,

    #[argh(option, short = 'e')]
    /// override the path to the environment configuration file (file path)
    pub env: Option<String>,

    #[argh(option)]
    /// produce output for a machine in the specified format; available formats: "json",
    /// "json-pretty"
    pub machine: Option<Format>,

    #[argh(option)]
    /// create a stamp file at the given path containing the exit code
    pub stamp: Option<String>,

    #[argh(option, short = 't')]
    #[ffx_config_default("target.default")]
    /// apply operations across single or multiple targets
    pub target: Option<String>,

    #[argh(option, short = 'T')]
    #[ffx_config_default(key = "proxy.timeout_secs", default = "1.0")]
    /// override default proxy timeout
    pub timeout: Option<f64>,

    #[argh(option, short = 'l', long = "log-level")]
    #[ffx_config_default(key = "log.level", default = "Debug")]
    /// sets the log level for ffx output (default = Debug). Other possible values are Info, Error,
    /// Warn, and Trace. Can be persisted via log.level config setting.
    pub log_level: Option<String>,

    #[argh(option, long = "isolate-dir")]
    /// turn on isolation mode using the given directory to isolate all config and socket files into
    /// the specified directory. This overrides the FFX_ISOLATE_DIR env variable, which can also put
    /// ffx into this mode.
    pub isolate_dir: Option<PathBuf>,

    #[argh(switch, short = 'v', long = "verbose")]
    /// logs ffx output to stdio according to log level
    pub verbose: bool,

    #[argh(positional, greedy)]
    pub subcommand: Vec<String>,
}

impl Ffx {
    pub fn load_context(&self) -> Result<EnvironmentContext, anyhow::Error> {
        // Configuration initialization must happen before ANY calls to the config (or the cache won't
        // properly have the runtime parameters.
        let overrides = self.runtime_config_overrides();
        let runtime_args = ffx_config::runtime::populate_runtime(&*self.config, overrides)?;
        let env_path = self.env.as_ref().map(PathBuf::from);

        // If we're given an isolation setting, use that. Otherwise do a normal detection of the environment.
        match (self, std::env::var_os("FFX_ISOLATE_DIR")) {
            (Ffx { isolate_dir: Some(path), .. }, _) => Ok(EnvironmentContext::isolated(
                path.to_path_buf(),
                HashMap::from_iter(std::env::vars()),
                runtime_args,
                env_path,
            )),
            (_, Some(path_str)) => Ok(EnvironmentContext::isolated(
                PathBuf::from(path_str),
                HashMap::from_iter(std::env::vars()),
                runtime_args,
                env_path,
            )),
            _ => EnvironmentContext::detect(runtime_args, std::env::current_dir()?, env_path)
                .map_err(|e| ffx_error!(e).into()),
        }
    }

    pub async fn initialize_overnet(
        &self,
        hoist_cache_dir: &Path,
        router_interval: Option<Duration>,
        daemon_check: DaemonVersionCheck,
    ) -> Result<Injection> {
        // todo(fxb/108692) we should get this in the environment context instead and leave the global
        // hoist() unset for ffx but I'm leaving the last couple uses of it in place for the sake of
        // avoiding complicated merge conflicts with isolation. Once we're ready for that, this should be
        // `let Hoist = hoist::Hoist::new()...`
        let hoist = hoist::init_hoist_with(Hoist::with_cache_dir_maybe_router(
            hoist_cache_dir,
            router_interval,
        )?)
        .context("initializing hoist")?;

        let target = match self.target().await? {
            Some(t) => {
                if ffx_config::get("ffx.fastboot.inline_target").await.unwrap_or(false) {
                    Some(TargetKind::FastbootInline(t))
                } else {
                    Some(TargetKind::Normal(t))
                }
            }
            None => None,
        };

        Ok(Injection::new(daemon_check, hoist.clone(), self.machine, target))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn cmd_only_last_component() {
        let args = ["test/things/ffx", "--help"].map(String::from);
        let cmd_line = FfxCommandLine::new(None, args).expect("Command line should parse");
        assert_eq!(cmd_line.command, vec!["ffx"]);
        assert_eq!(cmd_line.args, vec!["--help"]);
    }

    #[test]
    fn cmd_override_invoke() {
        let args = ["test/things/ffx", "--help"].map(String::from);
        let cmd_line = FfxCommandLine::new(Some("tools/ffx".to_owned()), args)
            .expect("Command line should parse");
        assert_eq!(cmd_line.command, vec!["tools/ffx"]);
        assert_eq!(cmd_line.args, vec!["--help"]);
    }

    #[test]
    fn cmd_override_multiple_terms_invoke() {
        let args = ["test/things/ffx", "--help"].map(String::from);
        let cmd_line = FfxCommandLine::new(Some("fx ffx".to_owned()), args)
            .expect("Command line should parse");
        assert_eq!(cmd_line.command, vec!["fx", "ffx"]);
        assert_eq!(cmd_line.args, vec!["--help"]);
    }
}
