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
        fs::File,
        io::{BufReader, BufWriter},
        path::Path,
    },
};

pub struct FileBacked {
    data: Persistent,
}

impl FileBacked {
    fn reader<P>(path: &Option<P>) -> Result<Option<BufReader<File>>>
    where
        P: AsRef<Path>,
    {
        match path {
            Some(p) => {
                File::open(&p).map(|f| Some(BufReader::new(f))).context("opening read buffer")
            }
            None => Ok(None),
        }
    }

    /// Atomically write to the file by creating a temporary file and passing it
    /// to the closure, and atomically rename it to the destination file.
    fn with_writer<F>(&self, path: Option<&str>, f: F) -> Result<()>
    where
        F: FnOnce(Option<BufWriter<&mut tempfile::NamedTempFile>>) -> Result<()>,
    {
        if let Some(path) = path {
            let parent = Path::new(path).parent().unwrap_or_else(|| Path::new("."));
            let mut tmp = tempfile::NamedTempFile::new_in(parent)?;

            f(Some(BufWriter::new(&mut tmp)))?;

            tmp.persist(path)?;

            Ok(())
        } else {
            f(None)
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
                FileBacked::reader(build)?,
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
        // First save the config to a temp file in the same location as the file, then atomically
        // rename the file to the final location to avoid partially written files.

        self.with_writer(global.as_ref().map(|s| s.as_str()), |global| {
            self.with_writer(build.as_ref().map(|s| s.as_str()), |build| {
                self.with_writer(user.as_ref().map(|s| s.as_str()), |user| {
                    self.data.save(global, build, user)
                })
            })
        })
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

    pub fn set(&mut self, query: &ConfigQuery<'_>, value: Value) -> Result<bool> {
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
