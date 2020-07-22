// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{ReadConfig, WriteConfig},
    crate::persistent_config::Persistent,
    anyhow::{anyhow, Error},
    ffx_config_plugin_args::ConfigLevel,
    serde_json::Value,
    std::fs::{File, OpenOptions},
    std::io::{BufReader, BufWriter},
};

pub struct FileBacked {
    data: Persistent,
}

impl FileBacked {
    fn reader_from_ref(path: &Option<&String>) -> Result<Option<BufReader<File>>, Error> {
        match path {
            Some(p) => match File::open(p) {
                Ok(f) => Ok(Some(BufReader::new(f))),
                Err(e) => Err(anyhow!("Could not open file {}", e)),
            },
            None => Ok(None),
        }
    }

    fn reader(path: &Option<String>) -> Result<Option<BufReader<File>>, Error> {
        match path {
            Some(p) => match File::open(p) {
                Ok(f) => Ok(Some(BufReader::new(f))),
                Err(e) => Err(anyhow!("Could not open file {}", e)),
            },
            None => Ok(None),
        }
    }

    fn writer_from_ref(path: &Option<&String>) -> Result<Option<BufWriter<File>>, Error> {
        match path {
            Some(p) => {
                let file = OpenOptions::new().write(true).truncate(true).create(true).open(p);
                match file {
                    Ok(f) => Ok(Some(BufWriter::new(f))),
                    Err(e) => Err(anyhow!("Could not open file {}", e)),
                }
            }
            None => Ok(None),
        }
    }

    fn writer(path: &Option<String>) -> Result<Option<BufWriter<File>>, Error> {
        match path {
            Some(p) => {
                let file = OpenOptions::new().write(true).truncate(true).create(true).open(p);
                match file {
                    Ok(f) => Ok(Some(BufWriter::new(f))),
                    Err(e) => Err(anyhow!("Could not open file {}", e)),
                }
            }
            None => Ok(None),
        }
    }

    pub(crate) fn load(
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<Self, Error> {
        let data = Persistent::load(
            FileBacked::reader(global)?,
            FileBacked::reader_from_ref(build)?,
            FileBacked::reader(user)?,
        )?;
        Ok(Self { data })
    }

    pub(crate) fn save(
        &self,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<(), Error> {
        self.data.save(
            FileBacked::writer(global)?,
            FileBacked::writer_from_ref(build)?,
            FileBacked::writer(user)?,
        )
    }
}

impl ReadConfig for FileBacked {
    fn get(&self, key: &str) -> Option<Value> {
        self.data.get(key)
    }
}

impl WriteConfig for FileBacked {
    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
        self.data.set(level, key, value)
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error> {
        self.data.remove(level, key)
    }
}
