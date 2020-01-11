// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]

use {
    crate::constants::DAEMON,
    anyhow::Error,
    serde_derive::{Deserialize, Serialize},
    std::env::current_exe,
    std::fs::File,
    std::io,
    std::path::PathBuf,
};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Config {
    pub tools: Option<String>,
    pub daemon: Option<String>,
}

impl Config {
    pub fn new() -> Self {
        Config { tools: None, daemon: None }
    }

    pub fn load_from_config_data(&mut self, path: &str) -> Result<(), Error> {
        log::info!("Opening file: {}", path);
        let config_map: Config = serde_json::from_reader(io::BufReader::new(File::open(path)?))?;
        self.tools = config_map.tools;
        Ok(())
    }

    pub fn get_tool_path(&self, cmd: &str) -> PathBuf {
        self.get_path(&self.tools, cmd)
    }

    pub fn get_daemon_path(&self) -> PathBuf {
        self.get_path(&self.daemon, DAEMON)
    }

    fn get_path(&self, config_path: &Option<String>, cmd: &str) -> PathBuf {
        let mut path = match config_path {
            Some(ref p) => PathBuf::from(p),
            None => {
                let mut current_path = current_exe().unwrap();
                current_path.pop();
                current_path
            }
        };
        path.push(cmd);
        path
    }
}
