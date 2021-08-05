// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_bail_with_code},
    ffx_config::{
        add, api::query::ConfigQuery, api::ConfigError, env_file, environment::Environment, get,
        print_config, raw, remove, set, set_metrics_status, show_metrics_status, ConfigLevel,
    },
    ffx_config_plugin_args::{
        AddCommand, AnalyticsCommand, AnalyticsControlCommand, ConfigCommand, EnvAccessCommand,
        EnvCommand, EnvSetCommand, GetCommand, MappingMode, RemoveCommand, SetCommand, SubCommand,
    },
    ffx_core::ffx_plugin,
    serde_json::Value,
    std::collections::HashMap,
    std::fs::File,
    std::io::Write,
    std::path::PathBuf,
};

#[ffx_plugin()]
pub async fn exec_config(config: ConfigCommand) -> Result<()> {
    let writer = Box::new(std::io::stdout());
    match &config.sub {
        SubCommand::Env(env) => exec_env(env, writer),
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
    let query = ConfigQuery::new(
        get_cmd.name.as_ref().map(|s| s.as_str()),
        None,
        get_cmd.build_dir.as_ref().map(|s| s.as_str()),
        get_cmd.select,
    );
    match get_cmd.name.as_ref() {
        Some(_) => match get_cmd.process {
            MappingMode::Raw => {
                let value: Option<Value> = raw(query).await?;
                output(writer, value)
            }
            MappingMode::Substitute => {
                let value: std::result::Result<Vec<Value>, _> = get(query).await;
                output_array(writer, value)
            }
            MappingMode::SubstituteAndFlatten => {
                let value: Option<Value> = get(query).await?;
                output(writer, value)
            }
        },
        None => print_config(writer, &get_cmd.build_dir).await,
    }
}

async fn exec_set(set_cmd: &SetCommand) -> Result<()> {
    set((&set_cmd.name, &set_cmd.level, &set_cmd.build_dir), set_cmd.value.clone()).await
}

async fn exec_remove(remove_cmd: &RemoveCommand) -> Result<()> {
    remove((&remove_cmd.name, &remove_cmd.level, &remove_cmd.build_dir)).await
}

async fn exec_add(add_cmd: &AddCommand) -> Result<()> {
    add(
        (&add_cmd.name, &add_cmd.level, &add_cmd.build_dir),
        Value::String(format!("{}", add_cmd.value)),
    )
    .await
}

fn exec_env_set<W: Write + Sync>(
    mut writer: W,
    env: &mut Environment,
    s: &EnvSetCommand,
    file: PathBuf,
) -> Result<()> {
    if !file.exists() {
        writeln!(writer, "\"{}\" does not exist, creating empty json file", file.display())?;
        let mut file = File::create(&file).context("opening write buffer")?;
        file.write_all(b"{}").context("writing configuration file")?;
        file.sync_all().context("syncing configuration file to filesystem")?;
    }
    match &s.level {
        ConfigLevel::User => match env.user.as_mut() {
            Some(v) => *v = file.display().to_string(),
            None => env.user = Some(file.display().to_string()),
        },
        ConfigLevel::Build => match &s.build_dir {
            Some(build_dir) => match env.build.as_mut() {
                Some(b) => match b.get_mut(&s.file) {
                    Some(e) => *e = build_dir.to_string(),
                    None => {
                        b.insert(build_dir.to_string(), file.display().to_string());
                    }
                },
                None => {
                    let mut build = HashMap::new();
                    build.insert(build_dir.to_string(), file.display().to_string());
                    env.build = Some(build);
                }
            },
            None => ffx_bail!("Missing --build-dir flag"),
        },
        ConfigLevel::Global => match env.global.as_mut() {
            Some(v) => *v = file.display().to_string(),
            None => env.global = Some(file.display().to_string()),
        },
        _ => ffx_bail!("This configuration is not stored in the enivronment."),
    }
    env.save(&file)
}

fn exec_env<W: Write + Sync>(env_command: &EnvCommand, mut writer: W) -> Result<()> {
    let file = env_file().ok_or(anyhow!("Could not find environment file"))?;
    let mut env = Environment::load(&file)?;
    match &env_command.access {
        Some(a) => match a {
            EnvAccessCommand::Set(s) => exec_env_set(writer, &mut env, s, file),
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
