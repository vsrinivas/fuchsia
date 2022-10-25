// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use argh::{FromArgs, SubCommands};
use errors::{ffx_error, ResultExt};
use ffx_command::{DaemonVersionCheck, Ffx, FfxCommandLine, ToolRunner, ToolSuite};
use ffx_config::EnvironmentContext;
use ffx_lib_args::FfxBuiltIn;
use ffx_lib_sub_command::SubCommand;

/// The command to be invoked and everything it needs to invoke
struct FfxSubCommand {
    app: Ffx,
    context: EnvironmentContext,
    cmd: FfxBuiltIn,
}

/// The suite of commands FFX supports.
struct FfxSuite {
    app: Ffx,
    context: EnvironmentContext,
}

const CIRCUIT_REFRESH_RATE: std::time::Duration = std::time::Duration::from_millis(500);

impl ToolSuite for FfxSuite {
    fn from_env(app: &Ffx, env: &EnvironmentContext) -> Result<Self> {
        let app = app.clone();
        let context = env.clone();
        Ok(Self { app, context })
    }

    fn global_command_list() -> &'static [&'static argh::CommandInfo] {
        SubCommand::COMMANDS
    }

    fn try_from_args(
        &self,
        ffx_cmd: &FfxCommandLine,
        args: &[&str],
    ) -> Result<Option<Box<(dyn ToolRunner + 'static)>>, argh::EarlyExit> {
        let cmd = FfxBuiltIn::from_args(&Vec::from_iter(ffx_cmd.cmd_iter()), args)?;
        let context = self.context.clone();
        let app = self.app.clone();
        Ok(Some(Box::new(FfxSubCommand { cmd, context, app })))
    }
}

#[async_trait::async_trait(?Send)]
impl ToolRunner for FfxSubCommand {
    /// Whether the given subcommand forces logging to stdout
    fn forces_stdout_log(&self) -> bool {
        match &self.cmd {
            subcommand @ FfxBuiltIn { subcommand: Some(_) } if is_daemon(subcommand) => true,
            _ => false,
        }
    }

    async fn run(self: Box<Self>) -> Result<(), anyhow::Error> {
        use SubCommand::FfxSchema;
        match self.cmd {
            FfxBuiltIn { subcommand: Some(FfxSchema(_)) } => {
                Ok(ffx_lib_suite::ffx_plugin_writer_all_output(0))
            }
            subcommand => {
                if self.app.machine.is_some()
                    && !ffx_lib_suite::ffx_plugin_is_machine_supported(&subcommand)
                {
                    Err(anyhow::Error::new(ffx_error!(
                        "The machine flag is not supported for this subcommand"
                    )))
                } else {
                    run_legacy_subcommand(self.app, self.context, subcommand).await
                }
            }
        }
    }
}

async fn run_legacy_subcommand(
    app: Ffx,
    context: EnvironmentContext,
    subcommand: FfxBuiltIn,
) -> Result<()> {
    let router_interval = if is_daemon(&subcommand) { Some(CIRCUIT_REFRESH_RATE) } else { None };
    let cache_path = context.get_cache_path()?;
    std::fs::create_dir_all(&cache_path)?;
    let hoist_cache_dir = tempfile::tempdir_in(&cache_path)?;
    let injector = app
        .initialize_overnet(
            hoist_cache_dir.path(),
            router_interval,
            DaemonVersionCheck::SameBuildId(context.daemon_version_string()?),
        )
        .await?;
    ffx_lib_suite::ffx_plugin_impl(&injector, subcommand).await
}

fn is_daemon(subcommand: &FfxBuiltIn) -> bool {
    use ffx_daemon_plugin_args::FfxPluginCommand;
    use ffx_daemon_plugin_sub_command::SubCommand::FfxDaemonStart;
    use SubCommand::FfxDaemonPlugin;
    matches!(
        subcommand,
        FfxBuiltIn {
            subcommand: Some(FfxDaemonPlugin(FfxPluginCommand { subcommand: FfxDaemonStart(_) }))
        }
    )
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let result = ffx_command::run::<FfxSuite>().await;
    if let Err(err) = &result {
        let mut out = std::io::stderr();
        // abort hard on a failure to print the user error somehow
        errors::write_result(err, &mut out).unwrap();
        ffx_command::report_user_error(err).await.unwrap();
        ffx_config::print_log_hint(&mut out).await;
    }
    std::process::exit(result.exit_code());
}
