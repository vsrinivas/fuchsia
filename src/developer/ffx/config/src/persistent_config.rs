// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{ReadConfig, WriteConfig},
    crate::priority_config::Priority,
    anyhow::{anyhow, Error},
    ffx_config_plugin_args::ConfigLevel,
    serde_json::Value,
    std::io::{Read, Write},
};

pub(crate) struct Persistent {
    data: Priority,
}

impl Persistent {
    fn open<R: Read>(file: Option<R>) -> Result<Option<Value>, Error> {
        if file.is_none() {
            return Ok(None);
        }
        let config = serde_json::from_reader(file.unwrap());
        // If JSON is malformed, this will just overwrite if set is ever used.
        if config.is_err() {
            return Ok(None);
        }

        Ok(Some(config.unwrap()))
    }

    fn save_config<W: Write>(file: Option<W>, value: &Option<Value>) -> Result<(), Error> {
        if value.is_none() {
            // No reason to throw an error.
            return Ok(());
        }
        if file.is_none() {
            // If no option is supplied, just move on to the next - assume user doesn't want to
            // save this level.
            return Ok(());
        }
        match serde_json::to_writer_pretty(file.unwrap(), value.as_ref().unwrap()) {
            Err(e) => Err(anyhow!("Could not write config file: {}", e)),
            Ok(_) => Ok(()),
        }
    }

    pub(crate) fn load<R: Read>(
        global: Option<R>,
        build: Option<R>,
        user: Option<R>,
    ) -> Result<Self, Error> {
        Ok(Self {
            data: Priority::new(
                Persistent::open(user)?,
                Persistent::open(build)?,
                Persistent::open(global)?,
            ),
        })
    }

    pub(crate) fn save<W: Write>(
        &self,
        global: Option<W>,
        build: Option<W>,
        user: Option<W>,
    ) -> Result<(), Error> {
        Persistent::save_config(user, &self.data.user)?;
        Persistent::save_config(build, &self.data.build)?;
        Persistent::save_config(global, &self.data.global)?;
        Ok(())
    }
}

impl ReadConfig for Persistent {
    fn get(&self, key: &str) -> Option<Value> {
        self.data.get(key)
    }
}

impl WriteConfig for Persistent {
    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
        match level {
            ConfigLevel::Defaults => Err(anyhow!("Cannot override defaults")),
            _ => self.data.set(&level, key, value),
        }
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error> {
        match level {
            ConfigLevel::Defaults => Err(anyhow!("Cannot override defaults")),
            _ => self.data.remove(&level, key),
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use std::io::{BufReader, BufWriter};

    const USER: &'static str = r#"
        {
            "name": "User"
        }"#;

    const BUILD: &'static str = r#"
        {
            "name": "Build"
        }"#;

    const GLOBAL: &'static str = r#"
        {
            "name": "Global"
        }"#;

    #[test]
    fn test_persistent_build() -> Result<(), Error> {
        let mut user_file = String::from(USER);
        let mut build_file = String::from(BUILD);
        let mut global_file = String::from(GLOBAL);

        let persistent_config = Persistent::load(
            Some(BufReader::new(global_file.as_bytes())),
            Some(BufReader::new(build_file.as_bytes())),
            Some(BufReader::new(user_file.as_bytes())),
        )?;

        let value = persistent_config.get("name");
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("User")));

        let mut user_file_out = String::new();
        let mut build_file_out = String::new();
        let mut global_file_out = String::new();

        unsafe {
            persistent_config.save(
                Some(BufWriter::new(global_file_out.as_mut_vec())),
                Some(BufWriter::new(build_file_out.as_mut_vec())),
                Some(BufWriter::new(user_file_out.as_mut_vec())),
            )?;
        }

        // Remove whitespace
        user_file.retain(|c| !c.is_whitespace());
        build_file.retain(|c| !c.is_whitespace());
        global_file.retain(|c| !c.is_whitespace());
        user_file_out.retain(|c| !c.is_whitespace());
        build_file_out.retain(|c| !c.is_whitespace());
        global_file_out.retain(|c| !c.is_whitespace());

        assert_eq!(user_file, user_file_out);
        assert_eq!(build_file, build_file_out);
        assert_eq!(global_file, global_file_out);

        Ok(())
    }
}
