// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_bail_with_code},
    ffx_config::{
        api::ConfigError, env_file, environment::Environment, get, print_config,
        set_metrics_status, show_metrics_status, ConfigLevel,
    },
    ffx_config_plugin_args::{
        AddCommand, AnalyticsCommand, AnalyticsControlCommand, ConfigCommand, EnvAccessCommand,
        EnvCommand, EnvSetCommand, GetCommand, MappingMode, RemoveCommand, SetCommand, SubCommand,
    },
    ffx_core::ffx_plugin,
    serde_json::Value,
    std::fs::{File, OpenOptions},
    std::io::Write,
};

#[ffx_plugin()]
pub async fn exec_config(config: ConfigCommand) -> Result<()> {
    let writer = Box::new(std::io::stdout());
    match &config.sub {
        SubCommand::Env(env) => exec_env(env, writer).await,
        SubCommand::Get(get_cmd) => exec_get(get_cmd, writer).await,
        SubCommand::Set(set_cmd) => exec_set(set_cmd).await,
        SubCommand::Remove(remove_cmd) => exec_remove(remove_cmd).await,
        SubCommand::Add(add_cmd) => exec_add(add_cmd).await,
        SubCommand::Analytics(analytics_cmd) => exec_analytics(analytics_cmd).await,
    }
}

fn output<W: Write + Sync>(mut writer: W, value: Option<Value>) -> Result<()> {
    match value {
        Some(v) => writeln!(writer, "{}", v).map_err(|e| anyhow!("{}", e)),
        // Use 2 error code so wrapper scripts don't need check for the string to differentiate
        // errors.
        None => ffx_bail_with_code!(2, "Value not found"),
    }
}

fn output_array<W: Write + Sync>(
    mut writer: W,
    values: std::result::Result<Vec<Value>, ConfigError>,
) -> Result<()> {
    match values {
        Ok(v) => {
            if v.len() == 1 {
                writeln!(writer, "{}", v[0]).map_err(|e| anyhow!("{}", e))
            } else {
                writeln!(writer, "{}", Value::Array(v)).map_err(|e| anyhow!("{}", e))
            }
        }
        // Use 2 error code so wrapper scripts don't need check for the string to differentiate
        // errors.
        Err(_) => ffx_bail_with_code!(2, "Value not found"),
    }
}

async fn exec_get<W: Write + Sync>(get_cmd: &GetCommand, writer: W) -> Result<()> {
    match get_cmd.name.as_ref() {
        Some(_) => match get_cmd.process {
            MappingMode::Raw => {
                let value: Option<Value> = get_cmd.query().get_raw().await?;
                output(writer, value)
            }
            MappingMode::Substitute => {
                let value: std::result::Result<Vec<Value>, _> = get(get_cmd.query()).await;
                output_array(writer, value)
            }
            MappingMode::File => {
                let value = get_cmd.query().get_file().await?;
                output(writer, value)
            }
        },
        None => print_config(writer, get_cmd.query().get_build_dir().await.as_deref()).await,
    }
}

async fn exec_set(set_cmd: &SetCommand) -> Result<()> {
    set_cmd.query().set(set_cmd.value.clone()).await
}

async fn exec_remove(remove_cmd: &RemoveCommand) -> Result<()> {
    remove_cmd.query().remove().await
}

async fn exec_add(add_cmd: &AddCommand) -> Result<()> {
    add_cmd.query().add(Value::String(format!("{}", add_cmd.value))).await
}

async fn exec_env_set<W: Write + Sync>(
    mut writer: W,
    env: &mut Environment,
    s: &EnvSetCommand,
) -> Result<()> {
    let env_file = env.get_path().context("Trying to set on environment not backed by a file")?;

    if !env_file.exists() {
        writeln!(writer, "\"{}\" does not exist, creating empty json file", env_file.display())?;
        let mut file = File::create(&env_file).context("opening write buffer")?;
        file.write_all(b"{}").context("writing configuration file")?;
        file.sync_all().context("syncing configuration file to filesystem")?;
    }

    // Double check read/write permissions and create the file if it doesn't exist.
    let _ = OpenOptions::new().read(true).write(true).create(true).open(&s.file)?;

    match &s.level {
        ConfigLevel::User => env.set_user(Some(&s.file)),
        ConfigLevel::Build => {
            env.set_build(s.build_dir.as_deref().context("Missing --build-dir flag")?, &s.file)
        }
        ConfigLevel::Global => env.set_global(Some(&s.file)),
        _ => ffx_bail!("This configuration is not stored in the environment."),
    }
    env.save().await
}

async fn exec_env<W: Write + Sync>(env_command: &EnvCommand, mut writer: W) -> Result<()> {
    let file = env_file().ok_or(anyhow!("Could not find environment file"))?;
    let mut env = Environment::load(&file)?;
    match &env_command.access {
        Some(a) => match a {
            EnvAccessCommand::Set(s) => exec_env_set(writer, &mut env, s).await,
            EnvAccessCommand::Get(g) => {
                writeln!(writer, "{}", env.display(&g.level))?;
                Ok(())
            }
        },
        None => {
            writeln!(writer, "{}", env.display(&None))?;
            Ok(())
        }
    }
}

async fn exec_analytics(analytics_cmd: &AnalyticsCommand) -> Result<()> {
    let writer = Box::new(std::io::stdout());
    match &analytics_cmd.sub {
        AnalyticsControlCommand::Enable(_) => set_metrics_status(true).await?,
        AnalyticsControlCommand::Disable(_) => set_metrics_status(false).await?,
        AnalyticsControlCommand::Show(_) => show_metrics_status(writer).await?,
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use tempfile::NamedTempFile;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_exec_env_set_set_values() -> Result<()> {
        let writer = Vec::<u8>::new();
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let mut env = Environment::new_empty(Some(tmp_file.path()));
        let cmd =
            EnvSetCommand { file: "test.json".into(), level: ConfigLevel::User, build_dir: None };
        exec_env_set(writer, &mut env, &cmd).await?;
        let result = Environment::load(&tmp_file)?;
        assert_eq!(cmd.file, result.get_user().unwrap());
        Ok(())
    }
}
