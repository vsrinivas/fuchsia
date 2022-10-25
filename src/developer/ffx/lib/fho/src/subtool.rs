// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use argh::{FromArgs, SubCommand, SubCommands};
use async_trait::async_trait;
use errors::{ffx_error, ResultExt};
use ffx_command::{DaemonVersionCheck, Ffx, FfxCommandLine, ToolRunner, ToolSuite};
use ffx_config::EnvironmentContext;
use ffx_core::Injector;
use fidl::endpoints::Proxy;
use fidl_fuchsia_developer_ffx as ffx_fidl;

#[derive(FromArgs)]
#[argh(subcommand)]
enum FhoVersion<M: FfxMain> {
    //FhoVersion1(M),
    /// Run the tool as if under ffx
    Standalone(M::Command),
}

#[derive(FromArgs)]
/// Fuchsia Host Objects Runner
struct ToolCommand<M: FfxMain> {
    #[argh(subcommand)]
    subcommand: FhoVersion<M>,
}

struct FhoSuite<M> {
    ffx: Ffx,
    context: EnvironmentContext,
    _p: std::marker::PhantomData<fn(M) -> ()>,
}

impl<M> Clone for FhoSuite<M> {
    fn clone(&self) -> Self {
        Self { ffx: self.ffx.clone(), context: self.context.clone(), _p: self._p.clone() }
    }
}

struct FhoTool<M: FfxMain> {
    suite: FhoSuite<M>,
    command: Option<ToolCommand<M>>,
}

pub struct FhoEnvironment<'a> {
    pub ffx: &'a Ffx,
    pub context: &'a EnvironmentContext,
    pub injector: &'a dyn Injector,
}

#[async_trait(?Send)]
impl<M: FfxMain> ToolRunner for FhoTool<M> {
    fn forces_stdout_log(&self) -> bool {
        M::forces_stdout_log()
    }

    async fn run(self: Box<Self>) -> Result<(), anyhow::Error> {
        let cache_path = self.suite.context.get_cache_path()?;
        std::fs::create_dir_all(&cache_path)?;
        let hoist_cache_dir = tempfile::tempdir_in(&cache_path)?;
        let build_info = self.suite.context.build_info();
        let injector = self
            .suite
            .ffx
            .initialize_overnet(
                hoist_cache_dir.path(),
                None,
                DaemonVersionCheck::SameVersionInfo(build_info),
            )
            .await?;
        let env = FhoEnvironment {
            ffx: &self.suite.ffx,
            context: &self.suite.context,
            injector: &injector,
        };
        let mut main = match self.command.context("Tried to invoke command twice")?.subcommand {
            FhoVersion::Standalone(tool) => M::from_env(env, tool).await?,
        };
        main.main().await
    }
}

impl<M: FfxMain> ToolSuite for FhoSuite<M> {
    fn from_env(ffx: &Ffx, context: &EnvironmentContext) -> Result<Self, anyhow::Error> {
        let ffx = ffx.clone();
        let context = context.clone();
        Ok(Self { ffx: ffx, context: context, _p: Default::default() })
    }

    fn global_command_list() -> &'static [&'static argh::CommandInfo] {
        FhoVersion::<M>::COMMANDS
    }

    fn try_from_args(
        &self,
        cmd: &FfxCommandLine,
        args: &[&str],
    ) -> Result<Option<Box<dyn ToolRunner>>, argh::EarlyExit> {
        let found = FhoTool {
            suite: self.clone(),
            command: Some(ToolCommand::<M>::from_args(&Vec::from_iter(cmd.cmd_iter()), args)?),
        };
        Ok(Some(Box::new(found)))
    }
}

#[async_trait(?Send)]
pub trait FfxTool: Sized + 'static {
    type Command: FromArgs + SubCommand + 'static;

    fn forces_stdout_log() -> bool;
    async fn from_env(env: FhoEnvironment<'_>, cmd: Self::Command) -> Result<Self>;
}

#[async_trait(?Send)]
pub trait FfxMain: FfxTool {
    /// The entrypoint of the tool. Once FHO has set up the environment for the tool, this is
    /// invoked. Should not be invoked directly unless for testing.
    async fn main(&mut self) -> Result<()>;

    /// Executes the tool. This is intended to be invoked by the user in main.
    async fn execute_tool() {
        let result = ffx_command::run::<FhoSuite<Self>>().await;

        if let Err(err) = &result {
            let mut out = std::io::stderr();
            // abort hard on a failure to print the user error somehow
            errors::write_result(err, &mut out).unwrap();
            ffx_command::report_user_error(err).await.unwrap();
            ffx_config::print_log_hint(&mut out).await;
        }

        std::process::exit(result.exit_code());
    }
}

#[async_trait(?Send)]
pub trait TryFromEnv: Sized {
    async fn try_from_env(env: &FhoEnvironment<'_>) -> Result<Self>;
}

#[derive(Debug, Clone)]
pub struct DaemonProtocol<P: Clone> {
    proxy: P,
}

impl<P: Clone> DaemonProtocol<P> {
    pub fn new(proxy: P) -> Self {
        Self { proxy }
    }
}

impl<P: Clone> DaemonProtocol<P> {
    pub fn into_inner(self) -> P {
        self.proxy
    }
}

impl<P: Clone> std::ops::Deref for DaemonProtocol<P> {
    type Target = P;

    fn deref(&self) -> &Self::Target {
        &self.proxy
    }
}

#[async_trait(?Send)]
impl<P: Proxy + Clone> TryFromEnv for DaemonProtocol<P>
where
    P::Protocol: fidl::endpoints::DiscoverableProtocolMarker,
{
    async fn try_from_env(env: &FhoEnvironment<'_>) -> Result<Self> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<P::Protocol>()?;
        let daemon = env.injector.daemon_factory().await?;
        let svc_name = <P::Protocol as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME;

        daemon.connect_to_protocol(svc_name, server_end.into_channel()).await?.map_err(
            |e| -> anyhow::Error {
                match e {
                    ffx_fidl::DaemonError::ProtocolNotFound => ffx_error!(format!(
                        "The daemon protocol '{svc_name}' did not match any protocols on the daemon
If you are not developing this plugin or the protocol it connects to, then this is a bug

Please report it at http://fxbug.dev/new/ffx+User+Bug."
                    ))
                    .into(),
                    ffx_fidl::DaemonError::ProtocolOpenError => ffx_error!(format!(
                        "The daemon protocol '{svc_name}' failed to open on the daemon.

If you are developing the protocol, there may be an internal failure when invoking the start
function. See the ffx.daemon.log for details at `ffx config get log.dir -p sub`.

If you are NOT developing this plugin or the protocol it connects to, then this is a bug.

Please report it at http://fxbug.dev/new/ffx+User+Bug."
                    ))
                    .into(),
                    unexpected => ffx_error!(format!(
"While attempting to open the daemon protocol '{svc_name}', received an unexpected error:

{unexpected:?}

This is not intended behavior and is a bug.
Please report it at http://fxbug.dev/new/ffx+User+Bug."

                                    ))
                    .into(),
                }
            },
        )?;
        Ok(DaemonProtocol { proxy })
    }
}

#[async_trait(?Send)]
impl TryFromEnv for ffx_fidl::DaemonProxy {
    async fn try_from_env(env: &FhoEnvironment<'_>) -> Result<Self> {
        env.injector.daemon_factory().await
    }
}

#[async_trait(?Send)]
impl TryFromEnv for ffx_fidl::TargetProxy {
    async fn try_from_env(env: &FhoEnvironment<'_>) -> Result<Self> {
        env.injector.target_factory().await
    }
}

#[async_trait(?Send)]
impl TryFromEnv for ffx_fidl::FastbootProxy {
    async fn try_from_env(env: &FhoEnvironment<'_>) -> Result<Self> {
        env.injector.fastboot_factory().await
    }
}

#[async_trait(?Send)]
impl TryFromEnv for ffx_writer::Writer {
    async fn try_from_env(env: &FhoEnvironment<'_>) -> Result<Self> {
        env.injector.writer().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // This keeps the macros from having compiler errors.
    use crate as fho;
    use crate::testing;
    use argh::FromArgs;
    use async_trait::async_trait;
    use fho_macro::FfxTool;

    struct NewTypeString(String);

    #[async_trait(?Send)]
    impl TryFromEnv for NewTypeString {
        async fn try_from_env(_env: &FhoEnvironment<'_>) -> Result<Self> {
            Ok(Self(String::from("foobar")))
        }
    }

    #[derive(FromArgs)]
    #[argh(subcommand, name = "fake", description = "fake command")]
    struct FakeCommand {
        #[argh(positional)]
        /// just needs a doc here so the macro doesn't complain.
        stuff: String,
    }

    #[derive(FfxTool)]
    #[ffx(forces_stdout_logs)]
    struct FakeTool {
        from_env_string: NewTypeString,
        #[command]
        fake_command: FakeCommand,
        writer: ffx_writer::Writer,
    }

    #[async_trait(?Send)]
    impl FfxMain for FakeTool {
        async fn main(&mut self) -> Result<()> {
            assert_eq!(self.from_env_string.0, "foobar");
            assert_eq!(self.fake_command.stuff, "stuff");
            self.writer.line("junk-line").unwrap();
            Ok(())
        }
    }

    // The main testing part will happen in the `main()` function of the tool.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_fake_tool() {
        let context = ffx_config::EnvironmentContext::default();
        let injector = testing::FakeInjectorBuilder::new()
            .writer_closure(|| async { Ok(ffx_writer::Writer::new(None)) })
            .build();
        // Runs the command line tool as if under ffx (first version of fho invocation).
        let ffx_cmd_line = ffx_command::FfxCommandLine::new(
            None,
            vec!["ffx".to_owned(), "fake".to_owned(), "stuff".to_owned()],
        )
        .unwrap();
        let ffx = ffx_cmd_line.parse::<FhoSuite<FakeTool>>();
        let tool_cmd = ToolCommand::<FakeTool>::from_args(
            &Vec::from_iter(ffx_cmd_line.cmd_iter()),
            &Vec::from_iter(ffx_cmd_line.args_iter()),
        )
        .unwrap();
        let fho_env = FhoEnvironment { ffx: &ffx, context: &context, injector: &injector };
        let mut fake_tool = match tool_cmd.subcommand {
            FhoVersion::Standalone(t) => FakeTool::from_env(fho_env, t).await.unwrap(),
        };
        fake_tool.main().await.unwrap();
    }
}
