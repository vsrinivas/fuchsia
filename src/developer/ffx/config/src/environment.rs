// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::lockfile::{Lockfile, LockfileCreateError},
    crate::paths::get_default_user_file_path,
    crate::ConfigLevel,
    anyhow::{bail, Context, Result},
    errors::ffx_error,
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        fmt,
        fs::{File, OpenOptions},
        io::{BufReader, Write},
        path::{Path, PathBuf},
        time::Duration,
    },
    tracing::{error, info},
};

#[derive(Clone, Debug, Default, PartialEq, Deserialize, Serialize)]
struct EnvironmentFiles {
    user: Option<PathBuf>,
    build: Option<HashMap<PathBuf, PathBuf>>,
    global: Option<PathBuf>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Environment {
    path: Option<PathBuf>,
    files: EnvironmentFiles,
}

impl Environment {
    /// Creates a new empty env that will be saved to a specific path, but is initialized
    /// with no settings.
    pub fn new_empty(path: Option<&Path>) -> Self {
        let path = path.map(Path::to_owned);
        Self { path, ..Self::default() }
    }

    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path = path.as_ref().to_owned();

        // Grab the lock because we're reading from the environment file.
        let lockfile = Self::lock_env(&path)?;
        Self::load_with_lock(lockfile, path)
    }

    /// Checks if we can manage to open the given environment file's lockfile,
    /// as well as each configuration file referenced by it, and returns the lockfile
    /// owner if we can't. Will return a normal error via result if any non-lockfile
    /// error is encountered while processing the files.
    ///
    /// Used to implement diagnostics for `ffx doctor`.
    pub fn check_locks(
        path: &Path,
    ) -> Result<Vec<(PathBuf, Result<PathBuf, LockfileCreateError>)>> {
        let path = path.to_owned();
        let (lock_path, env) = match Self::lock_env(&path) {
            Ok(lockfile) => {
                (lockfile.path().to_owned(), Self::load_with_lock(lockfile, path.clone())?)
            }
            Err(e) => return Ok(vec![(path, Err(e))]),
        };

        let mut checked = vec![(path, Ok(lock_path))];

        if let Some(user) = env.files.user {
            let res = Lockfile::lock_for(&user, Duration::from_secs(1));
            checked.push((user, res.map(|lock| lock.path().to_owned())));
        }
        if let Some(global) = env.files.global {
            let res = Lockfile::lock_for(&global, Duration::from_secs(1));
            checked.push((global, res.map(|lock| lock.path().to_owned())));
        }
        for (_, build) in env.files.build.unwrap_or_default() {
            let res = Lockfile::lock_for(&build, Duration::from_secs(1));
            checked.push((build, res.map(|lock| lock.path().to_owned())));
        }

        Ok(checked)
    }

    pub async fn save(&self) -> Result<()> {
        match &self.path {
            Some(path) => {
                // First save the config to a temp file in the same location as the file, then atomically
                // rename the file to the final location to avoid partially written files.
                let parent = path.parent().unwrap_or_else(|| Path::new("."));
                let mut tmp = tempfile::NamedTempFile::new_in(parent)?;

                // Grab the lock because we're writing to the environment file.
                let _e = Self::lock_env(path)?;
                serde_json::to_writer_pretty(&mut tmp, &self.files)
                    .context("writing environment to disk")?;

                tmp.flush().context("flushing environment")?;

                let _ = tmp.persist(path)?;

                crate::cache::invalidate().await;

                Ok(())
            }
            None => Err(anyhow::anyhow!("Tried to save environment with no path set")),
        }
    }

    fn load_with_lock(_lockfile: Lockfile, path: PathBuf) -> Result<Self> {
        let file = File::open(&path).context("opening file for read")?;

        let files = serde_json::from_reader(BufReader::new(file))
            .context("reading environment from disk")?;
        Ok(Self { path: Some(path), files })
    }

    fn lock_env(path: &Path) -> Result<Lockfile, LockfileCreateError> {
        Lockfile::lock_for(path, Duration::from_secs(2)).map_err(|e| {
            error!("Failed to create a lockfile for environment file {path}. Check that {lockpath} doesn't exist and can be written to. Ownership information: {owner:#?}", path=path.display(), lockpath=e.lock_path.display(), owner=e.owner);
            e
        })
    }

    pub fn get_path(&self) -> Option<&Path> {
        self.path.as_deref()
    }

    pub fn get_user(&self) -> Option<&Path> {
        self.files.user.as_deref()
    }
    pub fn set_user(&mut self, to: Option<&Path>) {
        self.files.user = to.map(Path::to_owned);
    }

    pub fn get_global(&self) -> Option<&Path> {
        self.files.global.as_deref()
    }
    pub fn set_global(&mut self, to: Option<&Path>) {
        self.files.global = to.map(Path::to_owned);
    }

    pub fn get_build(&self, for_dir: Option<&Path>) -> Option<&Path> {
        for_dir
            .and_then(|dir| self.files.build.as_ref().and_then(|dirs| dirs.get(dir)))
            .map(PathBuf::as_ref)
    }
    pub fn set_build(&mut self, for_dir: &Path, to: &Path) {
        let build_dirs = match &mut self.files.build {
            Some(build_dirs) => build_dirs,
            None => self.files.build.get_or_insert_with(Default::default),
        };
        build_dirs.insert(for_dir.to_owned(), to.to_owned());
    }

    fn display_user(&self) -> String {
        self.files
            .user
            .as_ref()
            .map_or_else(|| format!(" User: none\n"), |u| format!(" User: {}\n", u.display()))
    }

    fn display_build(&self) -> String {
        let mut res = format!(" Build:");
        match self.files.build.as_ref() {
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
        self.files
            .global
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
        let _e = Self::lock_env(path)?;
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
    pub async fn check(&mut self, level: &ConfigLevel, build_dir: Option<&Path>) -> Result<()> {
        match level {
            ConfigLevel::User => {
                if let None = self.files.user {
                    let default_path = get_default_user_file_path();
                    // This will override the config file if it exists.  This would happen anyway
                    // because of the cache.
                    let mut file = File::create(&default_path).context("opening write buffer")?;
                    file.write_all(b"{}").context("writing default user configuration file")?;
                    file.sync_all()
                        .context("syncing default user configuration file to filesystem")?;

                    self.files.user = Some(default_path);
                    self.save().await?;
                }
            }
            ConfigLevel::Global => {
                if let None = self.files.global {
                    bail!(
                        "Global configuration not set. Use 'ffx config env set' command \
                         to setup the environment."
                    );
                }
            }
            ConfigLevel::Build => match build_dir {
                Some(b_dir) => {
                    let build_dirs = match &mut self.files.build {
                        Some(build_dirs) => build_dirs,
                        None => self.files.build.get_or_insert_with(Default::default),
                    };
                    if !build_dirs.contains_key(b_dir) {
                        let config = b_dir.with_extension("json");
                        if !config.is_file() {
                            info!("Build configuration file for '{b_dir}' does not exist yet, will create it by default at '{config}' if a value is set", b_dir=b_dir.display(), config=config.display());
                        }
                        build_dirs.insert(b_dir.to_owned(), config);
                        self.save().await?;
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_loading_and_saving_environment() {
        let env: EnvironmentFiles = serde_json::from_str(ENVIRONMENT).unwrap();

        // Write out the initial test environment.
        let mut tmp_path = NamedTempFile::new().unwrap();
        serde_json::to_writer(&mut tmp_path, &env).unwrap();
        tmp_path.flush().unwrap();

        // Load the environment back in, and make sure it's correct.
        let env_load = Environment::load(tmp_path.path()).unwrap();
        assert_eq!(env, env_load.files);

        // Remove the file to prevent a spurious success
        std::fs::remove_file(tmp_path.path())
            .expect("Temporary env file wasn't available to remove");

        // Save the environment, then read the saved file and make sure it's correct.
        env_load.save().await.unwrap();
        tmp_path.flush().unwrap();

        let env_file = fs::read(tmp_path.path()).unwrap();
        let env_save: EnvironmentFiles = serde_json::from_slice(&env_file).unwrap();

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

        env.check(&ConfigLevel::Build, Some(&build_dir_path))
            .await
            .expect("Setting build level environment to automatic path should work");

        if let Some(build_configs) = Environment::load(&env_path)
            .expect("should be able to load the test-configured env-file.")
            .files
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
        let mut env = Environment::new_empty(Some(&env_path));
        let mut config_map = std::collections::HashMap::new();
        config_map.insert(build_dir_path.clone(), build_dir_config.clone());
        env.files.build = Some(config_map);
        env.save().await.expect("Should be able to save the configured environment");

        env.check(&ConfigLevel::Build, Some(&build_dir_path))
            .await
            .expect("Setting build level environment to automatic path should work");

        if let Some(build_configs) = Environment::load(&env_path)
            .expect("should be able to load the manually configured env-file.")
            .files
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
