// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::paths::get_default_user_file_path,
    crate::ConfigLevel,
    anyhow::{bail, Context, Result},
    errors::ffx_error,
    log::info,
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        fmt,
        fs::{File, OpenOptions},
        io::{BufReader, Write},
        path::{Path, PathBuf},
        sync::Mutex,
    },
};

#[derive(Clone, Debug, Default, PartialEq, Deserialize, Serialize)]
pub struct Environment {
    user: Option<PathBuf>,
    build: Option<HashMap<PathBuf, PathBuf>>,
    global: Option<PathBuf>,
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
    static ref ENV_MUTEX: Mutex<()> = Mutex::default();
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

    pub fn get_user(&self) -> Option<&Path> {
        self.user.as_deref()
    }
    pub fn set_user(&mut self, to: Option<&Path>) {
        self.user = to.map(Path::to_owned);
    }

    pub fn get_global(&self) -> Option<&Path> {
        self.global.as_deref()
    }
    pub fn set_global(&mut self, to: Option<&Path>) {
        self.global = to.map(Path::to_owned);
    }

    pub fn get_build(&self, for_dir: Option<&Path>) -> Option<&Path> {
        for_dir
            .and_then(|dir| self.build.as_ref().and_then(|dirs| dirs.get(dir)))
            .map(PathBuf::as_ref)
    }
    pub fn set_build(&mut self, for_dir: &Path, to: &Path) {
        let build_dirs = match &mut self.build {
            Some(build_dirs) => build_dirs,
            None => self.build.get_or_insert_with(Default::default),
        };
        build_dirs.insert(for_dir.to_owned(), to.to_owned());
    }

    fn display_user(&self) -> String {
        self.user
            .as_ref()
            .map_or_else(|| format!(" User: none\n"), |u| format!(" User: {}\n", u.display()))
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
                    res.push_str(&format!("  {} => {}\n", key.display(), val.display()));
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
            .map_or_else(|| format!(" Global: none\n"), |g| format!(" Global: {}\n", g.display()))
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
        let mut f = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(true)
            .open(path)
            .map_err(|e| {
                ffx_error!(
                    "Could not create envinronment file from given path \"{}\": {}",
                    path.display(),
                    e
                )
            })?;
        f.write_all(b"{}")?;
        f.sync_all()?;
        Ok(())
    }

    /// Checks the config files at the requested level to make sure they exist and are configured
    /// properly.
    pub fn check(
        &mut self,
        path: &Path,
        level: &ConfigLevel,
        build_dir: Option<&Path>,
    ) -> Result<()> {
        match level {
            ConfigLevel::User => {
                if let None = self.user {
                    let default_path = get_default_user_file_path();
                    // This will override the config file if it exists.  This would happen anyway
                    // because of the cache.
                    let mut file = File::create(&default_path).context("opening write buffer")?;
                    file.write_all(b"{}").context("writing default user configuration file")?;
                    file.sync_all()
                        .context("syncing default user configuration file to filesystem")?;
                    self.user = Some(default_path);
                    self.save(path)?;
                }
            }
            ConfigLevel::Global => {
                if let None = self.global {
                    bail!(
                        "Global configuration not set. Use 'ffx config env set' command \
                         to setup the environment."
                    );
                }
            }
            ConfigLevel::Build => match build_dir {
                Some(b_dir) => {
                    let build_dirs = match &mut self.build {
                        Some(build_dirs) => build_dirs,
                        None => self.build.get_or_insert_with(Default::default),
                    };
                    if !build_dirs.contains_key(b_dir) {
                        let config = b_dir.with_extension("json");
                        if !config.is_file() {
                            info!("Build configuration file for '{b_dir}' does not exist yet, will create it by default at '{config}' if a value is set", b_dir=b_dir.display(), config=config.display());
                        }
                        build_dirs.insert(b_dir.to_owned(), config);
                        self.save(path)?;
                    }
                }
                None => bail!("Cannot set a build configuration without a build directory."),
            },
            _ => bail!("This config level is not writable."),
        }
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn build_config_autoconfigure() {
        let temp = tempfile::tempdir().expect("temporary build directory");
        let temp_dir = std::fs::canonicalize(temp.path()).expect("canonical temp path");
        let build_dir_path = temp_dir.join("build");
        let build_dir_config = temp_dir.join("build.json");
        let env_path = temp_dir.join("env.json");

        assert!(!env_path.is_file(), "Environment file shouldn't exist yet");
        Environment::init_env_file(&env_path)
            .expect("Should be able to initialize the environment file");
        let mut env = Environment::load(&env_path).expect("Should be able to load the environment");

        env.check(&env_path, &ConfigLevel::Build, Some(&build_dir_path))
            .expect("Setting build level environment to automatic path should work");

        if let Some(build_configs) = Environment::load(&env_path)
            .expect("should be able to load the test-configured env-file.")
            .build
        {
            match build_configs.get(&build_dir_path) {
                Some(config) if config == &build_dir_config => (),
                Some(config) => panic!("Build directory config file was wrong. Expected: {build_dir_config}, got: {config})", build_dir_config=build_dir_config.display(), config=config.display()),
                None => panic!("No build directory config was set"),
            }
        } else {
            panic!("No build configurations set after setting a configuration value");
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn build_config_manual_configure() {
        let temp = tempfile::tempdir().expect("temporary build directory");
        let temp_dir = std::fs::canonicalize(temp.path()).expect("canonical temp path");
        let build_dir_path = temp_dir.join("build");
        let build_dir_config = temp_dir.join("build-manual.json");
        let env_path = temp_dir.join("env.json");

        assert!(!env_path.is_file(), "Environment file shouldn't exist yet");
        let mut env = Environment::default();
        let mut config_map = std::collections::HashMap::new();
        config_map.insert(build_dir_path.clone(), build_dir_config.clone());
        env.build = Some(config_map);
        env.save(&env_path).expect("Should be able to save the configured environment");

        env.check(&env_path, &ConfigLevel::Build, Some(&build_dir_path))
            .expect("Setting build level environment to automatic path should work");

        if let Some(build_configs) = Environment::load(&env_path)
            .expect("should be able to load the manually configured env-file.")
            .build
        {
            match build_configs.get(&build_dir_path) {
                Some(config) if config == &build_dir_config => (),
                Some(config) => panic!("Build directory config file was wrong. Expected: {build_dir_config}, got: {config})", build_dir_config=build_dir_config.display(), config=config.display()),
                None => panic!("No build directory config was set"),
            }
        } else {
            panic!("No build configurations set after setting a configuration value");
        }
    }
}
