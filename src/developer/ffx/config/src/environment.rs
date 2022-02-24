// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ConfigLevel,
    anyhow::{Context, Result},
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        fmt,
        fs::File,
        io::{BufReader, Write},
        path::Path,
        sync::Mutex,
    },
};

#[derive(Clone, Debug, Default, PartialEq, Deserialize, Serialize)]
pub struct Environment {
    pub user: Option<String>,
    pub build: Option<HashMap<String, String>>,
    pub global: Option<String>,
}

// This lock protects from concurrent [Environment]s from modifying the same underlying file.
// While in the normal case we typically only have one [Environment], it's possible to have
// multiple instances during tests. If we're not careful, it's possible concurrent [Environment]s
// could stomp on each other if they happen to use the same underlying file. To protect against
// this, we hold a lock while we read or write to the underlying file.
//
// It is inefficient to hold the lock for all [Environment] files, since we only need it when
// we're reading and writing to the same file. We could be more efficient if we a global map to
// control access to individual files, but we only encounter multiple [Environment]s in tests, so
// it's probably not worth the overhead.
lazy_static::lazy_static! {
    static ref ENV_MUTEX: Mutex<Option<String>> = Mutex::new(None);
}

impl Environment {
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path = path.as_ref();

        // Grab the lock because we're reading from the environment file.
        let _e = ENV_MUTEX.lock().unwrap();
        let file = File::open(path).context("opening file for read")?;

        serde_json::from_reader(BufReader::new(file)).context("reading environment from disk")
    }

    pub fn save<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let path = path.as_ref();

        // First save the config to a temp file in the same location as the file, then atomically
        // rename the file to the final location to avoid partially written files.
        let parent = path.parent().unwrap_or_else(|| Path::new("."));
        let mut tmp = tempfile::NamedTempFile::new_in(parent)?;

        // Grab the lock because we're writing to the environment file.
        let _e = ENV_MUTEX.lock().unwrap();
        serde_json::to_writer_pretty(&mut tmp, &self).context("writing environment to disk")?;

        tmp.flush().context("flushing environment")?;

        let _ = tmp.persist(path)?;

        Ok(())
    }

    fn display_user(&self) -> String {
        self.user.as_ref().map_or_else(|| format!(" User: none\n"), |u| format!(" User: {}\n", u))
    }

    fn display_build(&self) -> String {
        let mut res = format!(" Build:");
        match self.build.as_ref() {
            Some(m) => {
                if m.is_empty() {
                    res.push_str(&format!("  none\n"));
                }
                res.push_str(&format!("\n"));
                for (key, val) in m.iter() {
                    res.push_str(&format!("  {} => {}\n", key, val));
                }
            }
            None => {
                res.push_str(&format!("  none\n"));
            }
        }
        res
    }

    fn display_global(&self) -> String {
        self.global
            .as_ref()
            .map_or_else(|| format!(" Global: none\n"), |g| format!(" Global: {}\n", g))
    }

    pub fn display(&self, level: &Option<ConfigLevel>) -> String {
        level.map_or_else(
            || {
                let mut res = format!("\nEnvironment:\n");
                res.push_str(&self.display_user());
                res.push_str(&self.display_build());
                res.push_str(&self.display_global());
                res
            },
            |l| match l {
                ConfigLevel::User => self.display_user(),
                ConfigLevel::Build => self.display_build(),
                ConfigLevel::Global => self.display_global(),
                _ => format!(" This level is not saved in the environment file."),
            },
        )
    }

    pub fn init_env_file(path: &Path) -> Result<()> {
        let _e = ENV_MUTEX.lock().unwrap();
        let mut f = File::create(path)?;
        f.write_all(b"{}")?;
        f.sync_all()?;
        Ok(())
    }
}

impl fmt::Display for Environment {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{}", self.display(&None))
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, std::fs, tempfile::NamedTempFile};

    const ENVIRONMENT: &'static str = r#"
        {
            "user": "/tmp/user.json",
            "build": {
                "/tmp/build/1": "/tmp/build/1/build.json"
            },
            "global": "/tmp/global.json"
        }"#;

    #[test]
    fn test_loading_and_saving_environment() {
        let env: Environment = serde_json::from_str(ENVIRONMENT).unwrap();

        // Write out the initial test environment.
        let mut tmp_load = NamedTempFile::new().unwrap();
        serde_json::to_writer(&mut tmp_load, &env).unwrap();
        tmp_load.flush().unwrap();

        // Load the environment back in, and make sure it's correct.
        let env_load = Environment::load(tmp_load.path()).unwrap();
        assert_eq!(env, env_load);

        // Save the environment, then read the saved file and make sure it's correct.
        let mut tmp_save = NamedTempFile::new().unwrap();
        env.save(tmp_save.path()).unwrap();
        tmp_save.flush().unwrap();

        let env_file = fs::read(tmp_save.path()).unwrap();
        let env_save: Environment = serde_json::from_slice(&env_file).unwrap();

        assert_eq!(env, env_save);
    }
}
