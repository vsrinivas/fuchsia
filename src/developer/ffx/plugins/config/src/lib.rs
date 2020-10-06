// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_config::{
        add, api::query::ConfigQuery, api::ConfigError, env_file, environment::Environment, get,
        print_config, raw, remove, set, ConfigLevel,
    },
    ffx_config_plugin_args::{
        AddCommand, ConfigCommand, EnvAccessCommand, EnvCommand, EnvSetCommand, GetCommand,
        MappingMode, OutputType, RemoveCommand, SetCommand, SubCommand,
    },
    ffx_core::{ffx_bail, ffx_plugin},
    serde_json::Value,
    std::collections::HashMap,
    std::io::Write,
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
    }
}

fn json_output<W: Write + Sync>(mut writer: W, value: Option<Value>) -> Result<()> {
    match value {
        Some(v) => writeln!(writer, "{}", v).map_err(|e| anyhow!("{}", e)),
        None => ffx_bail!("Value not found"),
    }
}

fn human_readable<W: Write + Sync>(mut writer: W, name: &str, value: Option<Value>) -> Result<()> {
    match value {
        Some(v) => writeln!(writer, "{}: {}", name, v).map_err(|e| anyhow!("{}", e)),
        None => writeln!(writer, "{}: none", name).map_err(|e| anyhow!("{}", e)),
    }
}

fn json_output_array<W: Write + Sync>(
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
        Err(_) => ffx_bail!("Value not found"),
    }
}

fn human_readable_array<W: Write + Sync>(
    mut writer: W,
    name: &str,
    values: std::result::Result<Vec<Value>, ConfigError>,
) -> Result<()> {
    match values {
        Ok(v) => {
            if v.len() == 1 {
                writeln!(writer, "{}: {}", name, v[0]).map_err(|e| anyhow!("{}", e))
            } else {
                writeln!(writer, "{}: {}", name, Value::Array(v)).map_err(|e| anyhow!("{}", e))
            }
        }
        Err(_) => writeln!(writer, "{}: none", name).map_err(|e| anyhow!("{}", e)),
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
        Some(name) => match get_cmd.process {
            MappingMode::Raw => {
                let value: Option<Value> = raw(query).await?;
                match get_cmd.output {
                    OutputType::HumanReadable => human_readable(writer, name, value),
                    OutputType::Json => json_output(writer, value),
                }
            }
            MappingMode::Substitute => {
                let value: std::result::Result<Vec<Value>, _> = get(query).await;
                match get_cmd.output {
                    OutputType::HumanReadable => human_readable_array(writer, name, value),
                    OutputType::Json => json_output_array(writer, value),
                }
            }
            MappingMode::SubstituteAndFlatten => {
                let value: Option<Value> = get(query).await?;
                match get_cmd.output {
                    OutputType::HumanReadable => human_readable(writer, name, value),
                    OutputType::Json => json_output(writer, value),
                }
            }
        },
        None => print_config(writer, &get_cmd.build_dir).await,
    }
}

async fn exec_set(set_cmd: &SetCommand) -> Result<()> {
    set(
        (&set_cmd.name, &set_cmd.level, &set_cmd.build_dir),
        Value::String(format!("{}", set_cmd.value)),
    )
    .await
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

fn exec_env_set(env: &mut Environment, s: &EnvSetCommand, file: String) -> Result<()> {
    let file_str = format!("{}", s.file);
    match &s.level {
        ConfigLevel::User => match env.user.as_mut() {
            Some(v) => *v = file_str,
            None => env.user = Some(file_str),
        },
        ConfigLevel::Build => match &s.build_dir {
            Some(build_dir) => match env.build.as_mut() {
                Some(b) => match b.get_mut(&s.file) {
                    Some(e) => *e = build_dir.to_string(),
                    None => {
                        b.insert(build_dir.to_string(), file_str);
                    }
                },
                None => {
                    let mut build = HashMap::new();
                    build.insert(build_dir.to_string(), file_str);
                    env.build = Some(build);
                }
            },
            None => ffx_bail!("Missing --build-dir flag"),
        },
        ConfigLevel::Global => match env.global.as_mut() {
            Some(v) => *v = file_str,
            None => env.global = Some(file_str),
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
            EnvAccessCommand::Set(s) => exec_env_set(&mut env, s, file),
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
