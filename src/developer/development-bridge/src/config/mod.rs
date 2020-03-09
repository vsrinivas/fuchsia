// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::command::{find_env_file, load_config_from_environment},
    crate::config::configuration::Config,
    crate::config::environment::Environment,
    anyhow::Error,
    serde_json::Value,
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
