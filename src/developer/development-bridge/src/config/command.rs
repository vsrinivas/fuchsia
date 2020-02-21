// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::args::{
        ConfigCommand, EnvAccessCommand, EnvCommand, EnvGetCommand, EnvSetCommand, GetCommand,
        SetCommand, SubCommand,
    },
    crate::config::configuration::{Config, ConfigLevel, FileBackedConfig},
    crate::config::environment::Environment,
    anyhow::{anyhow, Error},
    serde_json::Value,
    std::{
        collections::HashMap,
        env,
        fs::{File, OpenOptions},
        io::{stdout, Write},
        path::PathBuf,
    },
};

const ENV_FILE: &str = ".ffx_env";

pub fn exec_config(config: ConfigCommand) -> Result<(), Error> {
    match config.sub {
        SubCommand::Env(env) => exec_env(env),
        SubCommand::Get(get) => exec_get(get),
        SubCommand::Set(set) => exec_set(set),
    };
    Ok(())
}

fn save_config_from_environment(
    env: &Environment,
    config: &mut FileBackedConfig,
    build_dir: Option<String>,
) -> Result<(), Error> {
    match build_dir {
        Some(b) => config.save(&env.global, &env.build.as_ref().and_then(|c| c.get(&b)), &env.user),
        None => config.save(&env.global, &None, &env.user),
    }
}

fn load_config_from_environment(
    env: &Environment,
    build_dir: &Option<String>,
) -> Result<Box<FileBackedConfig>, Error> {
    match build_dir {
        Some(b) => Ok(Box::new(FileBackedConfig::load(
            &env.defaults,
            &env.global,
            &env.build.as_ref().and_then(|c| c.get(b)),
            &env.user,
        )?)),
        None => Ok(Box::new(FileBackedConfig::load(&env.defaults, &env.global, &None, &env.user)?)),
    }
}

fn exec_get(get: GetCommand) -> Result<(), Error> {
    let file = find_env_file()?;
    let env = Environment::load(&file)?;
    let config = load_config_from_environment(&env, &get.build_dir)?;
    match config.get(&get.name) {
        Some(v) => println!("{}: {}", get.name, v),
        None => println!("{}: none", get.name),
    }
    Ok(())
}

fn exec_set(set: SetCommand) -> Result<(), Error> {
    let file = find_env_file()?;
    let env = Environment::load(&file)?;
    let mut config = load_config_from_environment(&env, &set.build_dir)?;
    config.set(&set.level, &set.name, Value::String(set.value))?;
    save_config_from_environment(&env, &mut config, set.build_dir)
}

fn init_env_file(path: &PathBuf) -> Result<(), Error> {
    let mut f = File::create(path)?;
    f.write_all(b"{}")?;
    f.sync_all()?;
    Ok(())
}

// TODO(fxr/45489): replace with the dirs::config_dir when the crate is included in third_party
// https://docs.rs/dirs/1.0.5/dirs/fn.config_dir.html
fn find_env_dir() -> Result<String, Error> {
    match env::var("HOME").or_else(|_| env::var("HOMEPATH")) {
        Ok(dir) => Ok(dir),
        Err(e) => Err(anyhow!("Could not determing environment directory: {}", e)),
    }
}

fn find_env_file() -> Result<String, Error> {
    let mut env_path = PathBuf::from(find_env_dir()?);
    env_path.push(ENV_FILE);

    if !env_path.is_file() {
        println!("Init env");
        init_env_file(&env_path);
    }
    match env_path.to_str() {
        Some(f) => Ok(String::from(f)),
        None => Err(anyhow!("Could not find environment file")),
    }
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

fn exec_env(env_command: EnvCommand) -> Result<(), Error> {
    let file = find_env_file()?;
    let mut env = Environment::load(&file)?;
    match env_command.access {
        Some(a) => match a {
            EnvAccessCommand::Set(s) => exec_env_set(&mut env, s, file),
            EnvAccessCommand::Get(g) => {
                println!("{}", env.display(&g.level));
                Ok(())
            }
        },
        None => Err(anyhow!("Missing flags.  Try `ffx config env help`")),
    }
}
