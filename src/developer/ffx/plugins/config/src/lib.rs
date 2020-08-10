// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    ffx_config::{environment::Environment, find_env_file, get, print, remove, set},
    ffx_config_plugin_args::{
        ConfigCommand, ConfigLevel, EnvAccessCommand, EnvCommand, EnvSetCommand, GetCommand,
        RemoveCommand, SetCommand, SubCommand,
    },
    ffx_core::ffx_plugin,
    serde_json::Value,
    std::collections::HashMap,
    std::io::Write,
};

#[ffx_plugin()]
pub async fn exec_config(config: ConfigCommand) -> Result<()> {
    let writer = Box::new(std::io::stdout());
    match &config.sub {
        SubCommand::Env(env) => exec_env(env, writer),
        SubCommand::Get(get) => exec_get(get, writer).await,
        SubCommand::Set(set) => exec_set(set).await,
        SubCommand::Remove(remove) => exec_remove(remove).await,
    }
}

async fn exec_get<W: Write + Sync>(get: &GetCommand, mut writer: W) -> Result<()> {
    match get.name.as_ref() {
        Some(name) => {
            if get.substitute {
                match get!(sub, name).await? {
                    Some(v) => writeln!(writer, "{}: {}", name, v)?,
                    None => writeln!(writer, "{}: none", name)?,
                }
            } else {
                match get!(name).await? {
                    Some(v) => writeln!(writer, "{}: {}", name, v)?,
                    None => writeln!(writer, "{}: none", name)?,
                }
            }
        }
        None => {
            print!(writer, &get.build_dir).await?;
        }
    }
    Ok(())
}

async fn exec_set(set: &SetCommand) -> Result<()> {
    set!(&set.level, &set.name, Value::String(format!("{}", set.value)), &set.build_dir).await
}

async fn exec_remove(set: &RemoveCommand) -> Result<()> {
    remove!(&set.level, &set.name, &set.build_dir).await
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
            None => bail!("Missing --build-dir flag"),
        },
        ConfigLevel::Global => match env.global.as_mut() {
            Some(v) => *v = file_str,
            None => env.global = Some(file_str),
        },
        _ => bail!("This configuration is not stored in the enivronment."),
    }
    env.save(&file)
}

fn exec_env<W: Write + Sync>(env_command: &EnvCommand, mut writer: W) -> Result<()> {
    let file = find_env_file()?;
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
