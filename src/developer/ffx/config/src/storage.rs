// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::query::SelectMode,
    crate::environment::Environment,
    crate::lockfile::Lockfile,
    crate::nested::{nested_get, nested_remove, nested_set},
    crate::ConfigLevel,
    anyhow::{bail, Context, Result},
    config_macros::include_default,
    futures::{stream::FuturesUnordered, StreamExt},
    serde::de::DeserializeOwned,
    serde_json::{Map, Value},
    std::{
        fmt,
        fs::OpenOptions,
        io::{BufReader, BufWriter, Read, Write},
        path::{Path, PathBuf},
    },
    tracing::error,
};

/// The type of a configuration level's mapping.
pub type ConfigMap = Map<String, Value>;

/// An individually loaded configuration file, including the path it came from
/// if it was loaded from disk.
#[derive(Debug, Clone)]
pub struct ConfigFile {
    path: Option<PathBuf>,
    contents: ConfigMap,
    dirty: bool,
}

#[derive(Debug, Clone)]
pub struct Config {
    default: ConfigMap,
    global: Option<ConfigFile>,
    user: Option<ConfigFile>,
    build: Option<ConfigFile>,
    runtime: ConfigMap,
}

struct PriorityIterator<'a> {
    curr: Option<ConfigLevel>,
    config: &'a Config,
}

impl<'a> Iterator for PriorityIterator<'a> {
    type Item = Option<&'a ConfigMap>;

    fn next(&mut self) -> Option<Self::Item> {
        use ConfigLevel::*;
        self.curr = ConfigLevel::next(self.curr);
        match self.curr {
            Some(Runtime) => Some(Some(&self.config.runtime)),
            Some(Build) => Some(self.config.build.as_ref().map(|file| &file.contents)),
            Some(User) => Some(self.config.user.as_ref().map(|file| &file.contents)),
            Some(Global) => Some(self.config.global.as_ref().map(|file| &file.contents)),
            Some(Default) => Some(Some(&self.config.default)),
            None => None,
        }
    }
}

/// Reads a JSON formatted reader permissively, returning None if for whatever reason
/// the file couldn't be read.
///
/// If the JSON is malformed, it will just get overwritten if set is ever used.
/// (TODO: Validate above assumptions)
fn read_json<T: DeserializeOwned>(file: impl Read) -> Option<T> {
    serde_json::from_reader(file).ok()
}

fn write_json<W: Write>(file: Option<W>, value: Option<&Value>) -> Result<()> {
    match (value, file) {
        (Some(v), Some(mut f)) => {
            serde_json::to_writer_pretty(&mut f, v).context("writing config file")?;
            f.flush().map_err(Into::into)
        }
        (_, _) => {
            // If either value or file are None, then return Ok(()). File being none will
            // presume the user doesn't want to save at this level.
            Ok(())
        }
    }
}

/// Atomically write to the file by creating a temporary file and passing it
/// to the closure, and atomically rename it to the destination file.
async fn with_writer<F>(path: Option<&Path>, f: F) -> Result<()>
where
    F: FnOnce(Option<BufWriter<&mut tempfile::NamedTempFile>>) -> Result<()>,
{
    if let Some(path) = path {
        let path = Path::new(path);
        let _lockfile = Lockfile::lock_for(path, std::time::Duration::from_secs(2)).await.map_err(|e| {
            error!("Failed to create a lockfile for {path}. Check that {lockpath} doesn't exist and can be written to. Ownership information: {owner:#?}", path=path.display(), lockpath=e.lock_path.display(), owner=e.owner);
            e
        })?;
        let parent = path.parent().unwrap_or_else(|| Path::new("."));
        let mut tmp = tempfile::NamedTempFile::new_in(parent)?;

        f(Some(BufWriter::new(&mut tmp)))?;

        tmp.persist(path)?;

        Ok(())
    } else {
        f(None)
    }
}

impl ConfigFile {
    fn from_map(path: Option<PathBuf>, contents: ConfigMap) -> Self {
        Self { path, contents, dirty: false }
    }

    fn from_buf(path: Option<PathBuf>, buffer: impl Read) -> Self {
        let contents = read_json(buffer)
            .as_ref()
            .and_then(Value::as_object)
            .cloned()
            .unwrap_or_else(Map::default);
        Self { path, contents, dirty: false }
    }

    fn from_file(path: &Path) -> Result<Self> {
        let buffer = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(&path)
            .map(BufReader::new)?;

        Ok(Self::from_buf(Some(path.to_owned()), buffer))
    }

    fn is_dirty(&self) -> bool {
        self.dirty
    }

    fn set(&mut self, key: &str, value: Value) -> Result<bool> {
        let key_vec: Vec<&str> = key.split('.').collect();
        let key = *key_vec.get(0).context("Can't set empty key")?;
        let changed = nested_set(&mut self.contents, key, &key_vec[1..], value);
        self.dirty = self.dirty || changed;
        Ok(changed)
    }

    pub fn remove(&mut self, key: &str) -> Result<()> {
        let key_vec: Vec<&str> = key.split('.').collect();
        let key = *key_vec.get(0).context("Can't remove empty key")?;
        self.dirty = true;
        nested_remove(&mut self.contents, key, &key_vec[1..])
    }

    async fn save(&mut self) -> Result<()> {
        // FIXME(81502): There is a race between the ffx CLI and the daemon service
        // in updating the config. We can lose changes if both try to change the
        // config at the same time. We can reduce the rate of races by only writing
        // to the config if the value actually changed.
        if self.is_dirty() {
            self.dirty = false;
            with_writer(self.path.as_deref(), |writer| {
                write_json(writer, Some(&Value::Object(self.contents.clone())))
            })
            .await
        } else {
            Ok(())
        }
    }
}

impl Default for ConfigFile {
    fn default() -> Self {
        Self::from_map(None, Map::default())
    }
}

impl Config {
    fn new(
        global: Option<ConfigFile>,
        build: Option<ConfigFile>,
        user: Option<ConfigFile>,
        runtime: ConfigMap,
    ) -> Self {
        let default = match include_default!() {
            Value::Object(obj) => obj,
            _ => panic!("Statically build default configuration was not an object"),
        };
        Self { user, build, global, runtime, default }
    }

    pub(crate) fn from_env(env: &Environment) -> Result<Self> {
        let build_conf = env.get_build();

        let user = env.get_user().map(ConfigFile::from_file).transpose()?;
        let build = build_conf.map(ConfigFile::from_file).transpose()?;
        let global = env.get_global().map(ConfigFile::from_file).transpose()?;

        Ok(Self::new(global, build, user, env.get_runtime_args().clone()))
    }

    #[cfg(test)]
    fn write<W: Write>(&self, global: Option<W>, build: Option<W>, user: Option<W>) -> Result<()> {
        write_json(
            user,
            self.user.as_ref().map(|file| Value::Object(file.contents.clone())).as_ref(),
        )?;
        write_json(
            build,
            self.build.as_ref().map(|file| Value::Object(file.contents.clone())).as_ref(),
        )?;
        write_json(
            global,
            self.global.as_ref().map(|file| Value::Object(file.contents.clone())).as_ref(),
        )?;
        Ok(())
    }

    pub(crate) async fn save(&mut self) -> Result<()> {
        let files = [&mut self.global, &mut self.build, &mut self.user];
        // Try to save all files and only fail out if any of them fail afterwards (with the first error). This hopefully mitigates
        // any weird partial-save issues, though there's no way to eliminate them altogether (short of filesystem
        // transactions)
        FuturesUnordered::from_iter(
            files.into_iter().filter_map(|file| file.as_mut()).map(ConfigFile::save),
        )
        .fold(Ok(()), |res, i| async { res.and_then(|_| i) })
        .await
    }

    pub fn get_level(&self, level: ConfigLevel) -> Option<&ConfigMap> {
        match level {
            ConfigLevel::Runtime => Some(&self.runtime),
            ConfigLevel::User => self.user.as_ref().map(|file| &file.contents),
            ConfigLevel::Build => self.build.as_ref().map(|file| &file.contents),
            ConfigLevel::Global => self.global.as_ref().map(|file| &file.contents),
            ConfigLevel::Default => Some(&self.default),
        }
    }

    pub fn get_in_level(&self, key: &str, level: ConfigLevel) -> Option<Value> {
        let key_vec: Vec<&str> = key.split('.').collect();
        nested_get(self.get_level(level), key_vec.get(0)?, &key_vec[1..]).cloned()
    }

    pub fn get(&self, key: &str, select: SelectMode) -> Option<Value> {
        let key_vec: Vec<&str> = key.split('.').collect();
        match select {
            SelectMode::First => {
                self.iter().find_map(|c| nested_get(c, *key_vec.get(0)?, &key_vec[1..])).cloned()
            }
            SelectMode::All => {
                let result: Vec<Value> = self
                    .iter()
                    .filter_map(|c| nested_get(c, *key_vec.get(0)?, &key_vec[1..]))
                    .cloned()
                    .collect();
                if result.len() > 0 {
                    Some(Value::Array(result))
                } else {
                    None
                }
            }
        }
    }

    pub fn set(&mut self, key: &str, level: ConfigLevel, value: Value) -> Result<bool> {
        let file = self.get_level_mut(level)?;
        file.set(key, value)
    }

    pub fn remove(&mut self, key: &str, level: ConfigLevel) -> Result<()> {
        let file = self.get_level_mut(level)?;
        file.remove(key)
    }

    /// Convenience method for getting the configured daemon socket, if any.
    pub fn get_ascendd_path(&self) -> Option<PathBuf> {
        self.get("overnet.socket", SelectMode::First)
            .as_ref()
            .and_then(Value::as_str)
            .map(PathBuf::from)
    }

    fn iter(&self) -> PriorityIterator<'_> {
        PriorityIterator { curr: None, config: self }
    }

    fn get_level_mut(&mut self, level: ConfigLevel) -> Result<&mut ConfigFile> {
        match level {
            ConfigLevel::Runtime => bail!("No mutable access to runtime level configuration"),
            ConfigLevel::User => self
                .user
                .as_mut()
                .context("Tried to write to unconfigured user level configuration"),
            ConfigLevel::Build => self
                .build
                .as_mut()
                .context("Tried to write to unconfigured build level configuration"),
            ConfigLevel::Global => self
                .global
                .as_mut()
                .context("Tried to write to unconfigured global level configuration"),
            ConfigLevel::Default => bail!("No mutable access to default level configuration"),
        }
    }
}

impl fmt::Display for Config {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(
            f,
            "FFX configuration can come from several places and has an inherent priority assigned\n\
            to the different ways the configuration is gathered. A configuration key can be set\n\
            in multiple locations but the first value found is returned. The following output\n\
            shows the locations checked in descending priority order.\n"
        )?;
        let mut iterator = self.iter();
        while let Some(next) = iterator.next() {
            if let Some(level) = iterator.curr {
                match level {
                    ConfigLevel::Runtime => {
                        write!(f, "Runtime Configuration")?;
                    }
                    ConfigLevel::User => {
                        write!(f, "User Configuration")?;
                    }
                    ConfigLevel::Build => {
                        write!(f, "Build Configuration")?;
                    }
                    ConfigLevel::Global => {
                        write!(f, "Global Configuration")?;
                    }
                    ConfigLevel::Default => {
                        write!(f, "Default Configuration")?;
                    }
                };
            }
            if let Some(value) = next {
                writeln!(f, "")?;
                writeln!(f, "{}", serde_json::to_string_pretty(&value).unwrap())?;
            } else {
                writeln!(f, ": {}", "none")?;
            }
            writeln!(f, "")?;
        }
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::{nested::RecursiveMap, EnvironmentContext};
    use regex::Regex;
    use serde_json::json;
    use std::collections::HashMap;
    use std::io::{BufReader, BufWriter};

    const ERROR: &'static [u8] = b"0";

    const USER: &'static [u8] = br#"
        {
            "name": "User"
        }"#;

    const BUILD: &'static [u8] = br#"
        {
            "name": "Build"
        }"#;

    const GLOBAL: &'static [u8] = br#"
        {
            "name": "Global"
        }"#;

    const DEFAULT: &'static [u8] = br#"
        {
            "name": "Default"
        }"#;

    const RUNTIME: &'static [u8] = br#"
        {
            "name": "Runtime"
        }"#;

    const MAPPED: &'static [u8] = br#"
        {
            "name": "TEST_MAP"
        }"#;

    const NESTED: &'static [u8] = br#"
        {
            "name": {
               "nested": "Nested"
            }
        }"#;

    const DEEP: &'static [u8] = br#"
        {
            "name": {
               "nested": {
                    "deep": {
                        "name": "TEST_MAP"
                    }
               }
            }
        }"#;

    const LITERAL: &'static [u8] = b"[]";

    #[test]
    fn test_persistent_build() -> Result<()> {
        let persistent_config = Config::new(
            Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            Map::default(),
        );

        let value = persistent_config.get("name", SelectMode::First);
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("Build")));

        let mut user_file_out = String::new();
        let mut build_file_out = String::new();
        let mut global_file_out = String::new();

        unsafe {
            persistent_config.write(
                Some(BufWriter::new(global_file_out.as_mut_vec())),
                Some(BufWriter::new(build_file_out.as_mut_vec())),
                Some(BufWriter::new(user_file_out.as_mut_vec())),
            )?;
        }

        // Remove whitespace
        let mut user_file = String::from_utf8_lossy(USER).to_string();
        let mut build_file = String::from_utf8_lossy(BUILD).to_string();
        let mut global_file = String::from_utf8_lossy(GLOBAL).to_string();
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

    #[test]
    fn test_priority_iterator() -> Result<()> {
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: serde_json::from_slice(RUNTIME)?,
        };

        let mut test_iter = test.iter();
        assert_eq!(test_iter.next(), Some(Some(&test.runtime)));
        assert_eq!(test_iter.next(), Some(test.build.as_ref().map(|file| &file.contents)));
        assert_eq!(test_iter.next(), Some(test.user.as_ref().map(|file| &file.contents)));
        assert_eq!(test_iter.next(), Some(test.global.as_ref().map(|file| &file.contents)));
        assert_eq!(test_iter.next(), Some(Some(&test.default)));
        assert_eq!(test_iter.next(), None);
        Ok(())
    }

    #[test]
    fn test_priority_iterator_with_nones() -> Result<()> {
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: None,
            global: None,
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };

        let mut test_iter = test.iter();
        assert_eq!(test_iter.next(), Some(Some(&test.runtime)));
        assert_eq!(test_iter.next(), Some(test.build.as_ref().map(|file| &file.contents)));
        assert_eq!(test_iter.next(), Some(test.user.as_ref().map(|file| &file.contents)));
        assert_eq!(test_iter.next(), Some(test.global.as_ref().map(|file| &file.contents)));
        assert_eq!(test_iter.next(), Some(Some(&test.default)));
        assert_eq!(test_iter.next(), None);
        Ok(())
    }

    #[test]
    fn test_get() -> Result<()> {
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };

        let value = test.get("name", SelectMode::First);
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("Build")));

        let test_build = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: None,
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };

        let value_build = test_build.get("name", SelectMode::First);
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), Value::String(String::from("User")));

        let test_global = Config {
            user: None,
            build: None,
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };

        let value_global = test_global.get("name", SelectMode::First);
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), Value::String(String::from("Global")));

        let test_default = Config {
            user: None,
            build: None,
            global: None,
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };

        let value_default = test_default.get("name", SelectMode::First);
        assert!(value_default.is_some());
        assert_eq!(value_default.unwrap(), Value::String(String::from("Default")));

        let test_none = Config {
            user: None,
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };

        let value_none = test_none.get("name", SelectMode::First);
        assert!(value_none.is_none());
        Ok(())
    }

    #[test]
    fn test_set_non_map_value() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(ERROR))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        test.set("name", ConfigLevel::User, Value::String(String::from("whatever")))?;
        let value = test.get("name", SelectMode::First);
        assert_eq!(value, Some(Value::String(String::from("whatever"))));
        Ok(())
    }

    #[test]
    fn test_get_nonexistent_config() -> Result<()> {
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };
        let value = test.get("field that does not exist", SelectMode::First);
        assert!(value.is_none());
        Ok(())
    }

    #[test]
    fn test_set() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };
        test.set("name", ConfigLevel::Build, Value::String(String::from("build-test")))?;
        let value = test.get("name", SelectMode::First);
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("build-test")));
        Ok(())
    }

    #[test]
    fn test_set_twice_does_not_change_config() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };
        assert!(test.set(
            "name",
            ConfigLevel::Build,
            Value::String(String::from("build-test1"))
        )?);
        assert_eq!(
            test.get("name", SelectMode::First).unwrap(),
            Value::String(String::from("build-test1"))
        );

        assert!(!test.set(
            "name",
            ConfigLevel::Build,
            Value::String(String::from("build-test1"))
        )?);
        assert_eq!(
            test.get("name", SelectMode::First).unwrap(),
            Value::String(String::from("build-test1"))
        );

        assert!(test.set(
            "name",
            ConfigLevel::Build,
            Value::String(String::from("build-test2"))
        )?);
        assert_eq!(
            test.get("name", SelectMode::First).unwrap(),
            Value::String(String::from("build-test2"))
        );

        Ok(())
    }

    #[test]
    fn test_set_build_from_none() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::default()),
            build: Some(ConfigFile::default()),
            global: Some(ConfigFile::default()),
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        let value_none = test.get("name", SelectMode::First);
        assert!(value_none.is_none());
        let error_set =
            test.set("name", ConfigLevel::Default, Value::String(String::from("default")));
        assert!(error_set.is_err(), "Should not be able to set default values at runtime");
        let value_default = test.get("name", SelectMode::First);
        assert!(
            value_default.is_none(),
            "Default value should be unset after failed attempt to set it"
        );
        test.set("name", ConfigLevel::Global, Value::String(String::from("global")))?;
        let value_global = test.get("name", SelectMode::First);
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), Value::String(String::from("global")));
        test.set("name", ConfigLevel::User, Value::String(String::from("user")))?;
        let value_user = test.get("name", SelectMode::First);
        assert!(value_user.is_some());
        assert_eq!(value_user.unwrap(), Value::String(String::from("user")));
        test.set("name", ConfigLevel::Build, Value::String(String::from("build")))?;
        let value_build = test.get("name", SelectMode::First);
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), Value::String(String::from("build")));
        Ok(())
    }

    #[test]
    fn test_remove() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };
        test.remove("name", ConfigLevel::User)?;
        let user_value = test.get("name", SelectMode::First);
        assert!(user_value.is_some());
        assert_eq!(user_value.unwrap(), Value::String(String::from("Build")));
        test.remove("name", ConfigLevel::Build)?;
        let global_value = test.get("name", SelectMode::First);
        assert!(global_value.is_some());
        assert_eq!(global_value.unwrap(), Value::String(String::from("Global")));
        test.remove("name", ConfigLevel::Global)?;
        let default_value = test.get("name", SelectMode::First);
        assert!(default_value.is_some());
        assert_eq!(default_value.unwrap(), Value::String(String::from("Default")));
        let error_removed = test.remove("name", ConfigLevel::Default);
        assert!(error_removed.is_err(), "Should not be able to remove a default value");
        let default_value = test.get("name", SelectMode::First);
        assert_eq!(
            default_value,
            Some(Value::String(String::from("Default"))),
            "value should still be default after trying to remove it (was {:?})",
            default_value
        );
        Ok(())
    }

    #[test]
    fn test_default() {
        let test = Config::new(None, None, None, Map::default());
        let default_value = test.get("log.enabled", SelectMode::First);
        assert_eq!(
            default_value.unwrap(),
            Value::Array(vec![Value::String("$FFX_LOG_ENABLED".to_string()), Value::Bool(true)])
        );
    }

    #[test]
    fn test_display() -> Result<()> {
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: ConfigMap::default(),
        };
        let output = format!("{}", test);
        assert!(output.len() > 0);
        let user_reg = Regex::new("\"name\": \"User\"").expect("test regex");
        assert_eq!(1, user_reg.find_iter(&output).count());
        let build_reg = Regex::new("\"name\": \"Build\"").expect("test regex");
        assert_eq!(1, build_reg.find_iter(&output).count());
        let global_reg = Regex::new("\"name\": \"Global\"").expect("test regex");
        assert_eq!(1, global_reg.find_iter(&output).count());
        let default_reg = Regex::new("\"name\": \"Default\"").expect("test regex");
        assert_eq!(1, default_reg.find_iter(&output).count());
        Ok(())
    }

    fn test_map(_ctx: &EnvironmentContext, value: Value) -> Option<Value> {
        value
            .as_str()
            .map(|s| match s {
                "TEST_MAP" => Value::String("passed".to_string()),
                _ => Value::String("failed".to_string()),
            })
            .or(Some(value))
    }

    #[test]
    fn test_mapping() -> Result<()> {
        let ctx = EnvironmentContext::isolated(
            "/tmp".into(),
            HashMap::default(),
            ConfigMap::default(),
            None,
        );
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(MAPPED))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        let test_mapping = "TEST_MAP".to_string();
        let test_passed = "passed".to_string();
        let mapped_value =
            test.get("name", SelectMode::First).as_ref().recursive_map(&ctx, &test_map);
        assert_eq!(mapped_value, Some(Value::String(test_passed)));
        let identity_value = test.get("name", SelectMode::First);
        assert_eq!(identity_value, Some(Value::String(test_mapping)));
        Ok(())
    }

    #[test]
    fn test_nested_get() -> Result<()> {
        let test = Config {
            user: None,
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: serde_json::from_slice(NESTED)?,
        };
        let value = test.get("name.nested", SelectMode::First);
        assert_eq!(value, Some(Value::String("Nested".to_string())));
        Ok(())
    }

    #[test]
    fn test_nested_get_should_return_sub_tree() -> Result<()> {
        let test = Config {
            user: None,
            build: None,
            global: None,
            default: serde_json::from_slice(DEFAULT)?,
            runtime: serde_json::from_slice(NESTED)?,
        };
        let value = test.get("name", SelectMode::First);
        assert_eq!(value, Some(serde_json::from_str("{\"nested\": \"Nested\"}")?));
        Ok(())
    }

    #[test]
    fn test_nested_get_should_return_full_match() -> Result<()> {
        let test = Config {
            user: None,
            build: None,
            global: None,
            default: serde_json::from_slice(NESTED)?,
            runtime: serde_json::from_slice(RUNTIME)?,
        };
        let value = test.get("name.nested", SelectMode::First);
        assert_eq!(value, Some(Value::String("Nested".to_string())));
        Ok(())
    }

    #[test]
    fn test_nested_get_should_map_values_in_sub_tree() -> Result<()> {
        let ctx = EnvironmentContext::isolated(
            "/tmp".into(),
            HashMap::default(),
            ConfigMap::default(),
            None,
        );
        let test = Config {
            user: None,
            build: None,
            global: None,
            default: serde_json::from_slice(NESTED)?,
            runtime: serde_json::from_slice(DEEP)?,
        };
        let value =
            test.get("name.nested", SelectMode::First).as_ref().recursive_map(&ctx, &test_map);
        assert_eq!(value, Some(serde_json::from_str("{\"deep\": {\"name\": \"passed\"}}")?));
        Ok(())
    }

    #[test]
    fn test_nested_set_from_none() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::default()),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        test.set("name.nested", ConfigLevel::User, Value::Bool(false))?;
        let nested_value = test.get("name", SelectMode::First);
        assert_eq!(nested_value, Some(serde_json::from_str("{\"nested\": false}")?));
        Ok(())
    }

    #[test]
    fn test_nested_set_from_already_populated_tree() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(NESTED))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        test.set("name.updated", ConfigLevel::User, Value::Bool(true))?;
        let expected = json!({
           "nested": "Nested",
           "updated": true
        });
        let nested_value = test.get("name", SelectMode::First);
        assert_eq!(nested_value, Some(expected));
        Ok(())
    }

    #[test]
    fn test_nested_set_override_literals() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(LITERAL))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        test.set("name.updated", ConfigLevel::User, Value::Bool(true))?;
        let expected = json!({
           "updated": true
        });
        let nested_value = test.get("name", SelectMode::First);
        assert_eq!(nested_value, Some(expected));
        test.set("name.updated", ConfigLevel::User, serde_json::from_slice(NESTED)?)?;
        let nested_value = test.get("name.updated.name.nested", SelectMode::First);
        assert_eq!(nested_value, Some(Value::String(String::from("Nested"))));
        Ok(())
    }

    #[test]
    fn test_nested_remove_from_none() -> Result<()> {
        let mut test = Config {
            user: None,
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        let result = test.remove("name.nested", ConfigLevel::User);
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_nested_remove_throws_error_if_key_not_found() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(NESTED))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        let result = test.remove("name.unknown", ConfigLevel::User);
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_nested_remove_deletes_literals() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(DEEP))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        test.remove("name.nested.deep.name", ConfigLevel::User)?;
        let value = test.get("name", SelectMode::First);
        assert_eq!(value, None);
        Ok(())
    }

    #[test]
    fn test_nested_remove_deletes_subtrees() -> Result<()> {
        let mut test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(DEEP))),
            build: None,
            global: None,
            default: ConfigMap::default(),
            runtime: ConfigMap::default(),
        };
        test.remove("name.nested", ConfigLevel::User)?;
        let value = test.get("name", SelectMode::First);
        assert_eq!(value, None);
        Ok(())
    }

    #[test]
    fn test_additive_mode() -> Result<()> {
        let test = Config {
            user: Some(ConfigFile::from_buf(None, BufReader::new(USER))),
            build: Some(ConfigFile::from_buf(None, BufReader::new(BUILD))),
            global: Some(ConfigFile::from_buf(None, BufReader::new(GLOBAL))),
            default: serde_json::from_slice(DEFAULT)?,
            runtime: serde_json::from_slice(RUNTIME)?,
        };
        let value = test.get("name", SelectMode::All);
        match value {
            Some(Value::Array(v)) => {
                assert_eq!(v.len(), 5);
                let mut v = v.into_iter();
                assert_eq!(v.next(), Some(Value::String("Runtime".to_string())));
                assert_eq!(v.next(), Some(Value::String("Build".to_string())));
                assert_eq!(v.next(), Some(Value::String("User".to_string())));
                assert_eq!(v.next(), Some(Value::String("Global".to_string())));
                assert_eq!(v.next(), Some(Value::String("Default".to_string())));
            }
            _ => anyhow::bail!("additive mode should return a Value::Array full of all values."),
        }
        Ok(())
    }
}
