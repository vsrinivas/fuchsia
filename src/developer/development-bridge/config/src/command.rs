// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::environment::Environment,
    crate::{
        find_env_file, get_config, remove_config_with_build_dir as remove_config,
        set_config_with_build_dir as set_config,
    },
    anyhow::{anyhow, Error},
    ffx_config_args::{
        ConfigCommand, EnvAccessCommand, EnvCommand, EnvSetCommand, GetCommand, RemoveCommand,
        SetCommand, SubCommand,
    },
    ffx_core::ConfigLevel,
    serde_json::Value,
    std::collections::HashMap,
    std::io::Write,
};

pub async fn exec_config<W: Write + Sync>(config: ConfigCommand, writer: W) -> Result<(), Error> {
    match config.sub {
        SubCommand::Env(env) => exec_env(env, writer),
        SubCommand::Get(get) => exec_get(get, writer).await,
        SubCommand::Set(set) => exec_set(set).await,
        SubCommand::Remove(remove) => exec_remove(remove).await,
    }
}

async fn exec_get<W: Write + Sync>(get: GetCommand, mut writer: W) -> Result<(), Error> {
    match get_config(&get.name, argh::from_env(), find_env_file()).await? {
        Some(v) => writeln!(writer, "{}: {}", get.name, v)?,
        None => writeln!(writer, "{}: none", get.name)?,
    };
    Ok(())
}

async fn exec_set(set: SetCommand) -> Result<(), Error> {
    set_config(
        set.level,
        &set.name,
        Value::String(set.value),
        set.build_dir,
        argh::from_env(),
        find_env_file(),
    )
    .await
}

async fn exec_remove(set: RemoveCommand) -> Result<(), Error> {
    remove_config(set.level, &set.name, set.build_dir, argh::from_env(), find_env_file()).await
}

fn exec_env_set(env: &mut Environment, s: EnvSetCommand, file: String) -> Result<(), Error> {
    match s.level {
        ConfigLevel::User => match env.user.as_mut() {
            Some(v) => *v = s.file,
            None => env.user = Some(s.file),
        },
        ConfigLevel::Build => match s.build_dir {
            Some(build_dir) => match env.build.as_mut() {
                Some(b) => match b.get_mut(&s.file) {
                    Some(e) => *e = build_dir,
                    None => {
                        b.insert(build_dir, s.file);
                    }
                },
                None => {
                    let mut build = HashMap::new();
                    build.insert(build_dir, s.file);
                    env.build = Some(build);
                }
            },
            None => return Err(anyhow!("Missing build-dir flag")),
        },
        ConfigLevel::Global => match env.global.as_mut() {
            Some(v) => *v = s.file,
            None => env.global = Some(s.file),
        },
        ConfigLevel::Defaults => return Err(anyhow!("Cannot overwrite the default config file")),
    }
    env.save(&file)
}

fn exec_env<W: Write + Sync>(env_command: EnvCommand, mut writer: W) -> Result<(), Error> {
    let file = find_env_file()?;
    let mut env = Environment::load(&file)?;
    match env_command.access {
        Some(a) => match a {
            EnvAccessCommand::Set(s) => exec_env_set(&mut env, s, file),
            EnvAccessCommand::Get(g) => {
                writeln!(writer, "{}", env.display(&g.level))?;
                Ok(())
            }
        },
        None => Err(anyhow!("Missing flags.  Try `ffx config env help`")),
    }
}
