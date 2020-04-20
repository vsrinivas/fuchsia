// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::Ffx,
    crate::config::configuration::Config,
    crate::config::configuration::FileBackedConfig,
    crate::config::environment::Environment,
    crate::constants::{ENV_FILE, LOG_DIR, LOG_ENABLED, SSH_PRIV, SSH_PUB},
    anyhow::{anyhow, Error},
    serde_json::Value,
    std::{
        env,
        fs::File,
        io::Write,
        path::{Path, PathBuf},
    },
};

pub mod args;
pub mod command;
pub mod configuration;
pub mod environment;

pub fn get_config(name: &str) -> Result<Option<Value>, Error> {
    get_config_with_build_dir(name, &None)
}

pub fn get_config_with_build_dir(
    name: &str,
    build_dir: &Option<String>,
) -> Result<Option<Value>, Error> {
    let file = find_env_file()?;
    let env = Environment::load(&file)?;
    let config = load_config_from_environment(&env, build_dir)?;
    Ok(config.get(name))
}

pub fn get_config_str(name: &str, default: &str) -> String {
    get_config(name)
        .unwrap_or(Some(Value::String(default.to_string())))
        .map_or(default.to_string(), |v| v.as_str().unwrap_or(default).to_string())
}

pub fn get_config_bool(name: &str, default: bool) -> bool {
    get_config(name).unwrap_or(Some(Value::Bool(default))).map_or(default, |v| {
        v.as_bool().unwrap_or(match v {
            Value::String(s) => s.parse().unwrap_or(default),
            _ => default,
        })
    })
}

// TODO(fxr/45489): replace with the dirs::config_dir when the crate is included in third_party
// https://docs.rs/dirs/1.0.5/dirs/fn.config_dir.html
fn find_env_dir() -> Result<String, Error> {
    match env::var("HOME").or_else(|_| env::var("HOMEPATH")) {
        Ok(dir) => Ok(dir),
        Err(e) => Err(anyhow!("Could not determing environment directory: {}", e)),
    }
}

fn init_env_file(path: &PathBuf) -> Result<(), Error> {
    let mut f = File::create(path)?;
    f.write_all(b"{}")?;
    f.sync_all()?;
    Ok(())
}

pub(crate) fn find_env_file() -> Result<String, Error> {
    let mut env_path = PathBuf::from(find_env_dir()?);
    env_path.push(ENV_FILE);

    if !env_path.is_file() {
        log::debug!("initializing environment {}", env_path.display());
        init_env_file(&env_path)?;
    }
    match env_path.to_str() {
        Some(f) => Ok(String::from(f)),
        None => Err(anyhow!("Could not find environment file")),
    }
}

pub(crate) fn save_config_from_environment(
    env: &Environment,
    config: &mut FileBackedConfig,
    build_dir: Option<String>,
) -> Result<(), Error> {
    match build_dir {
        Some(b) => config.save(&env.global, &env.build.as_ref().and_then(|c| c.get(&b)), &env.user),
        None => config.save(&env.global, &None, &env.user),
    }
}

pub(crate) fn load_config_from_environment(
    env: &Environment,
    build_dir: &Option<String>,
) -> Result<Box<FileBackedConfig>, Error> {
    let mut config = match build_dir {
        Some(b) => Box::new(FileBackedConfig::load(
            &env.defaults,
            &env.global,
            &env.build.as_ref().and_then(|c| c.get(b)),
            &env.user,
        )?),
        None => Box::new(FileBackedConfig::load(&env.defaults, &env.global, &None, &env.user)?),
    };

    config.data.heuristics.insert(SSH_PUB, find_ssh_keys);
    config.data.heuristics.insert(SSH_PRIV, find_ssh_keys);
    config.data.environment_variables.insert(LOG_DIR, vec!["FFX_LOG_DIR", "HOME", "HOMEPATH"]);
    config.data.environment_variables.insert(LOG_ENABLED, vec!["FFX_LOG_ENABLED"]);

    let cli: Ffx = argh::from_env();
    match cli.config {
        Some(config_str) => config_str.split(',').for_each(|c| {
            let s: Vec<&str> = c.trim().split('=').collect();
            if s.len() == 2 {
                config.data.runtime_config.insert(s[0].to_string(), s[1].to_string());
            }
        }),
        _ => {}
    };
    Ok(config)
}

fn find_ssh_keys(key: &str) -> Option<Value> {
    let k = if key == SSH_PUB { "authorized_keys" } else { "pkey" };
    match std::env::var("FUCHSIA_DIR") {
        Ok(r) => {
            if Path::new(&r).exists() {
                return Some(Value::String(String::from(format!("{}/.ssh/{}", r, k))));
            }
        }
        Err(_) => {
            if key != SSH_PUB {
                return None;
            }
        }
    }
    match std::env::var("HOME") {
        Ok(r) => {
            if Path::new(&r).exists() {
                Some(Value::String(String::from(format!("{}/.ssh/id_rsa.pub", r))))
            } else {
                None
            }
        }
        Err(_) => None,
    }
}
