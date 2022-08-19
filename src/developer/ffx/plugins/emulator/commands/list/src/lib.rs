// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use cfg_if::cfg_if;
use errors::ffx_bail;
use ffx_core::ffx_plugin;

use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::EmulatorEngine;
use ffx_emulator_list_args::ListCommand;

#[cfg(test)]
use mockall::{automock, predicate::*};

// Redeclare some methods we use from other crates so that
// we can mock them for tests.
#[cfg_attr(test, automock)]
#[allow(dead_code)]
mod modules {
    use super::*;

    pub(super) async fn get_all_instances(ffx_config: &FfxConfigWrapper) -> Result<Vec<String>> {
        ffx_emulator_common::instances::get_all_instances(ffx_config).await
    }

    pub(super) async fn get_engine_by_name(
        ffx_config: &FfxConfigWrapper,
        name: &mut Option<String>,
    ) -> Result<Box<dyn EmulatorEngine>> {
        ffx_emulator_commands::get_engine_by_name(ffx_config, name).await
    }
}

// if we're testing, use the mocked methods, otherwise use the
// ones from the other crates.
cfg_if! {
    if #[cfg(test)] {
        use self::mock_modules::get_all_instances;
        use self::mock_modules::get_engine_by_name;
    } else {
        use self::modules::get_all_instances;
        use self::modules::get_engine_by_name;
    }
}

// TODO(fxbug.dev/94232): Update this error message once shut down is more robust.
const BROKEN_MESSAGE: &str = r#"
One or more emulators are in a 'Broken' state. This is an uncommon state, but usually happens if
the Fuchsia source tree or SDK is updated while the emulator is still running. Communication with
a "Broken" emulator may still be possible, but errors will be encountered for any further `ffx emu`
commands. Running `ffx emu stop` will not shut down a broken emulator (this should be fixed as part
of fxbug.dev/94232), but it will clear that emulator's state from the system, so this error won't
appear anymore.
"#;

#[ffx_plugin()]
pub async fn list(_cmd: ListCommand) -> Result<()> {
    exec_list_impl(&mut std::io::stdout(), &mut std::io::stderr()).await
}

/// Entry point for the list command that allows specifying the writer for the output.
pub async fn exec_list_impl<W: std::io::Write, E: std::io::Write>(
    writer: &mut W,
    error_writer: &mut E,
) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let instance_list: Vec<Option<String>> = match get_all_instances(&ffx_config).await {
        Ok(list) => list.into_iter().map(|v| Some(v)).collect(),
        Err(e) => ffx_bail!("Error encountered looking up emulator instances: {:?}", e),
    };
    let mut broken = false;
    for mut some_name in instance_list {
        match get_engine_by_name(&ffx_config, &mut some_name).await {
            Ok(engine) => {
                let name = some_name.unwrap();
                if engine.is_running() {
                    writeln!(writer, "[Active]    {}", name)?;
                } else {
                    writeln!(writer, "[Inactive]  {}", name)?;
                }
            }
            Err(_) => {
                writeln!(
                    writer,
                    "[Broken]    {}",
                    some_name.unwrap_or("<unspecified>".to_string())
                )?;
                broken = true;
                continue;
            }
        };
    }
    if broken {
        writeln!(error_writer, "{}", BROKEN_MESSAGE)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::anyhow;
    use async_trait::async_trait;
    use ffx_emulator_config::{EmulatorConfiguration, EngineConsoleType, EngineType};
    use fidl_fuchsia_developer_ffx as ffx;
    use lazy_static::lazy_static;
    use std::{
        process::Command,
        str,
        sync::{Mutex, MutexGuard},
    };

    // Since we are mocking global methods, we need to synchronize
    // the setting of the expectations on the mock. This is done using a Mutex.
    lazy_static! {
        static ref MTX: Mutex<()> = Mutex::new(());
    }

    // When a test panics, it will poison the Mutex. Since we don't actually
    // care about the state of the data we ignore that it is poisoned and grab
    // the lock regardless.  If you just do `let _m = &MTX.lock().unwrap()`, one
    // test panicking will cause all other tests that try and acquire a lock on
    // that Mutex to also panic.
    fn get_lock(m: &'static Mutex<()>) -> MutexGuard<'static, ()> {
        match m.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        }
    }

    /// TestEngine is a test struct for implementing the EmulatorEngine trait
    /// Currently this one only exposes the running flag which is returned from
    /// EmulatorEngine::is_running().
    pub struct TestEngine {
        pub running_flag: bool,
    }

    #[async_trait]
    impl EmulatorEngine for TestEngine {
        async fn start(&mut self, _: Command, _: &ffx::TargetCollectionProxy) -> Result<i32> {
            todo!()
        }
        async fn stop(&self, _: &ffx::TargetCollectionProxy) -> Result<()> {
            todo!()
        }
        fn show(&self) {
            todo!()
        }
        async fn stage(&mut self) -> Result<()> {
            todo!()
        }
        fn validate(&self) -> Result<()> {
            todo!()
        }
        fn engine_type(&self) -> EngineType {
            EngineType::default()
        }
        fn is_running(&self) -> bool {
            self.running_flag
        }
        fn build_emulator_cmd(&self) -> Command {
            todo!()
        }
        fn emu_config(&self) -> &EmulatorConfiguration {
            todo!()
        }
        fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration {
            todo!()
        }
        fn attach(&self, _console: EngineConsoleType) -> Result<()> {
            todo!()
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_list() -> Result<()> {
        // get the lock for the mock, it is released when
        // the test exits.
        let _m = get_lock(&MTX);

        // no existing instances
        let ctx = mock_modules::get_all_instances_context();
        ctx.expect().returning(|_| Ok(vec![]));

        let engine_ctx = mock_modules::get_engine_by_name_context();
        engine_ctx.expect().times(0);

        let mut stdout: Vec<u8> = vec![];
        let mut stderr: Vec<u8> = vec![];
        exec_list_impl(&mut stdout, &mut stderr).await?;

        assert!(stdout.is_empty());
        assert!(stderr.is_empty());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_inactive_list() -> Result<()> {
        // get the lock for the mock, it is released when
        // the test exits.
        let _m = get_lock(&MTX);

        let ctx = mock_modules::get_all_instances_context();
        ctx.expect().returning(|_| Ok(vec!["notrunning_emu".to_string()]));

        let engine_ctx = mock_modules::get_engine_by_name_context();
        engine_ctx.expect().returning(|_, _| Ok(Box::new(TestEngine { running_flag: false })));

        let mut stdout: Vec<u8> = vec![];
        let mut stderr: Vec<u8> = vec![];
        exec_list_impl(&mut stdout, &mut stderr).await?;

        let stdout_expected = "[Inactive]  notrunning_emu\n";
        assert_eq!(str::from_utf8(&stdout)?, stdout_expected);
        assert!(stderr.is_empty());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_active_list() -> Result<()> {
        // get the lock for the mock, it is released when
        // the test exits.
        let _m = get_lock(&MTX);

        let ctx = mock_modules::get_all_instances_context();
        ctx.expect().returning(|_| Ok(vec!["running_emu".to_string()]));

        let engine_ctx = mock_modules::get_engine_by_name_context();
        engine_ctx.expect().returning(|_, _| Ok(Box::new(TestEngine { running_flag: true })));

        let mut stdout: Vec<u8> = vec![];
        let mut stderr: Vec<u8> = vec![];

        exec_list_impl(&mut stdout, &mut stderr).await?;

        let stdout_expected = "[Active]    running_emu\n";
        assert_eq!(str::from_utf8(&stdout)?, stdout_expected);
        assert!(stderr.is_empty());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error_list() -> Result<()> {
        // get the lock for the mock, it is released when
        // the test exits.
        let _m = get_lock(&MTX);

        let ctx = mock_modules::get_all_instances_context();
        ctx.expect().returning(|_| Ok(vec!["error_emu".to_string()]));

        let engine_ctx = mock_modules::get_engine_by_name_context();
        engine_ctx.expect().returning(|_, _| Err(anyhow!("This instance cannot be parsed")));

        let mut stdout: Vec<u8> = vec![];
        let mut stderr: Vec<u8> = vec![];

        exec_list_impl(&mut stdout, &mut stderr).await?;

        let stdout_expected = "[Broken]    error_emu\n";
        assert_eq!(str::from_utf8(&stdout)?, stdout_expected);
        assert_eq!(str::from_utf8(&stderr)?, format!("{}\n", BROKEN_MESSAGE));
        Ok(())
    }
}
