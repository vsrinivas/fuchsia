// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_config::{
        env_file, environment::Environment, get, print_config, raw, remove, set, ConfigLevel,
    },
    ffx_config_plugin_args::{
        ConfigCommand, EnvAccessCommand, EnvCommand, EnvSetCommand, GetCommand, RemoveCommand,
        SetCommand, SubCommand,
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
    }
}

async fn exec_get<W: Write + Sync>(get_cmd: &GetCommand, mut writer: W) -> Result<()> {
    match get_cmd.name.as_ref() {
        Some(name) => {
            let value: Option<Value> =
                if get_cmd.substitute { get(name).await? } else { raw(name).await? };
            match value {
                Some(v) => writeln!(writer, "{}: {}", name, v)?,
                None => writeln!(writer, "{}: none", name)?,
            }
        }
        None => {
            print_config(writer, &get_cmd.build_dir).await?;
        }
    }
    Ok(())
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
