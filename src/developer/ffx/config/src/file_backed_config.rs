// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::environment::Environment,
    crate::persistent_config::Persistent,
    crate::ConfigQuery,
    anyhow::{Context, Result},
    serde_json::Value,
    std::{
        fmt,
        fs::{File, OpenOptions},
        io::{BufReader, BufWriter},
    },
};

pub struct FileBacked {
    data: Persistent,
}

impl FileBacked {
    fn reader_from_ref(path: &Option<&String>) -> Result<Option<BufReader<File>>> {
        match path {
            Some(p) => {
                File::open(p).map(|f| Some(BufReader::new(f))).context("opening read buffer")
            }
            None => Ok(None),
        }
    }

    fn reader(path: &Option<String>) -> Result<Option<BufReader<File>>> {
        match path {
            Some(p) => {
                File::open(p).map(|f| Some(BufReader::new(f))).context("opening read buffer")
            }
            None => Ok(None),
        }
    }

    fn writer_from_ref(path: &Option<&String>) -> Result<Option<BufWriter<File>>> {
        match path {
            Some(p) => OpenOptions::new()
                .write(true)
                .truncate(true)
                .create(true)
                .open(p)
                .map(|f| Some(BufWriter::new(f)))
                .context("opening write buffer"),
            None => Ok(None),
        }
    }

    fn writer(path: &Option<String>) -> Result<Option<BufWriter<File>>> {
        match path {
            Some(p) => OpenOptions::new()
                .write(true)
                .truncate(true)
                .create(true)
                .open(p)
                .map(|f| Some(BufWriter::new(f)))
                .context("opening write buffer"),
            None => Ok(None),
        }
    }

    pub(crate) fn load(
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
        runtime: Option<Value>,
    ) -> Result<Self> {
        Ok(Self {
            data: Persistent::load(
                FileBacked::reader(global)?,
                FileBacked::reader_from_ref(build)?,
                FileBacked::reader(user)?,
                runtime,
            )?,
        })
    }

    pub(crate) fn save(
        &self,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<()> {
        self.data.save(
            FileBacked::writer(global)?,
            FileBacked::writer_from_ref(build)?,
            FileBacked::writer(user)?,
        )
    }

    pub(crate) fn new(
        env: &Environment,
        build_dir: &Option<String>,
        runtime: Option<Value>,
    ) -> Result<Self> {
        Self::load_from_environment(env, build_dir, runtime)
    }

    fn load_from_environment(
        env: &Environment,
        build_dir: &Option<String>,
        runtime: Option<Value>,
    ) -> Result<Self> {
        let runtime_clone = runtime.clone();
        build_dir.as_ref().map_or_else(
            || Self::load(&env.global, &None, &env.user, runtime),
            |b| {
                Self::load(
                    &env.global,
                    &env.build.as_ref().and_then(|c| c.get(b)),
                    &env.user,
                    runtime_clone,
                )
            },
        )
    }

    pub fn get<T: Fn(Value) -> Option<Value>>(
        &self,
        query: &ConfigQuery<'_>,
        mapper: &T,
    ) -> Option<Value> {
        self.data.get(query, mapper)
    }

    pub fn set(&mut self, query: &ConfigQuery<'_>, value: Value) -> Result<()> {
        self.data.set(query, value)
    }

    pub fn remove(&mut self, query: &ConfigQuery<'_>) -> Result<()> {
        self.data.remove(query)
    }
}

impl fmt::Display for FileBacked {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.data)
    }
}
