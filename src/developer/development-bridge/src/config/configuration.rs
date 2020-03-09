// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    serde_json::Value,
    std::collections::HashMap,
    std::fs::{File, OpenOptions},
    std::io::{BufReader, BufWriter, Read, Write},
};

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ConfigLevel {
    Defaults,
    Build,
    Global,
    User,
}

pub trait Config {
    fn get(&self, key: &str) -> Option<Value>;
    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error>;
    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error>;
}

struct ConfigData {
    defaults: Option<Value>,
    build: Option<Value>,
    global: Option<Value>,
    user: Option<Value>,
}

struct PriorityConfigIterator<'a> {
    curr: Option<ConfigLevel>,
    config: &'a ConfigData,
}

impl<'a> Iterator for PriorityConfigIterator<'a> {
    type Item = &'a Option<Value>;

    fn next(&mut self) -> Option<Self::Item> {
        match &self.curr {
            None => {
                self.curr = Some(ConfigLevel::User);
                Some(&self.config.user)
            }
            Some(level) => match level {
                ConfigLevel::User => {
                    self.curr = Some(ConfigLevel::Build);
                    Some(&self.config.build)
                }
                ConfigLevel::Build => {
                    self.curr = Some(ConfigLevel::Global);
                    Some(&self.config.global)
                }
                ConfigLevel::Global => {
                    self.curr = Some(ConfigLevel::Defaults);
                    Some(&self.config.defaults)
                }
                ConfigLevel::Defaults => None,
            },
        }
    }
}

impl ConfigData {
    fn iter(&self) -> PriorityConfigIterator<'_> {
        PriorityConfigIterator { curr: None, config: self }
    }
}

impl Config for ConfigData {
    fn get(&self, key: &str) -> Option<Value> {
        self.iter()
            .filter(|c| c.is_some())
            .filter_map(|c| c.as_ref().unwrap().as_object())
            .find_map(|c| c.get(key).cloned())
    }

    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
        let set = |config_data: &mut Option<Value>| match config_data {
            Some(config) => match config.as_object_mut() {
                Some(map) => match map.get_mut(key) {
                    Some(v) => {
                        *v = value;
                        Ok(())
                    }
                    None => {
                        map.insert(key.to_string(), value);
                        Ok(())
                    }
                },
                _ => {
                    return Err(anyhow!(
                        "Configuration already exists but it is is a JSON literal - not a map"
                    ))
                }
            },
            None => {
                let mut config = serde_json::Map::new();
                config.insert(key.to_string(), value);
                *config_data = Some(Value::Object(config));
                Ok(())
            }
        };
        match level {
            ConfigLevel::User => set(&mut self.user),
            ConfigLevel::Build => set(&mut self.build),
            ConfigLevel::Global => set(&mut self.global),
            ConfigLevel::Defaults => set(&mut self.defaults),
        }
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error> {
        let remove = |config_data: &mut Option<Value>| -> Result<(), Error> {
            match config_data {
                Some(config) => match config.as_object_mut() {
                    Some(map) => map
                        .remove(&key.to_string())
                        .ok_or_else(|| anyhow!("No config found matching {}", key))
                        .map(|_| ()),
                    None => Err(anyhow!("Config file parsing error")),
                },
                None => Ok(()),
            }
        };
        match level {
            ConfigLevel::User => remove(&mut self.user),
            ConfigLevel::Build => remove(&mut self.build),
            ConfigLevel::Global => remove(&mut self.global),
            ConfigLevel::Defaults => remove(&mut self.defaults),
        }
    }
}

struct PersistentConfig {
    data: ConfigData,
}

impl PersistentConfig {
    fn load<R: Read>(
        defaults: Option<R>,
        global: Option<R>,
        build: Option<R>,
        user: Option<R>,
    ) -> Result<Self, Error> {
        Ok(PersistentConfig {
            data: ConfigData {
                user: PersistentConfig::open(user)?,
                build: PersistentConfig::open(build)?,
                global: PersistentConfig::open(global)?,
                defaults: PersistentConfig::open(defaults)?,
            },
        })
    }

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

    fn save<W: Write>(
        &self,
        global: Option<W>,
        build: Option<W>,
        user: Option<W>,
    ) -> Result<(), Error> {
        PersistentConfig::save_config(user, &self.data.user)?;
        PersistentConfig::save_config(build, &self.data.build)?;
        PersistentConfig::save_config(global, &self.data.global)?;
        // Don't overwrite defaults file.
        Ok(())
    }
}

impl Config for PersistentConfig {
    fn get(&self, key: &str) -> Option<Value> {
        self.data.get(key)
    }

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

type Heuristic = fn(key: &str) -> Option<Value>;

pub struct HeuristicConfig {
    data: PersistentConfig,
    pub heuristics: HashMap<&'static str, Heuristic>,
}

impl HeuristicConfig {
    fn load<R: Read>(
        defaults: Option<R>,
        global: Option<R>,
        build: Option<R>,
        user: Option<R>,
    ) -> Result<Self, Error> {
        Ok(Self {
            data: PersistentConfig::load(defaults, global, build, user)?,
            heuristics: HashMap::new(),
        })
    }

    fn save<W: Write>(
        &self,
        global: Option<W>,
        build: Option<W>,
        user: Option<W>,
    ) -> Result<(), Error> {
        self.data.save(global, build, user)
    }
}

impl Config for HeuristicConfig {
    fn get(&self, key: &str) -> Option<Value> {
        self.data.get(key).or_else(|| match self.heuristics.get(key) {
            Some(r) => r(key),
            None => None,
        })
    }

    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
        self.data.set(level, key, value)
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error> {
        self.data.remove(level, key)
    }
}

pub struct FileBackedConfig {
    pub data: HeuristicConfig,
}

impl FileBackedConfig {
    pub fn load(
        defaults: &Option<String>,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<Self, Error> {
        let data = HeuristicConfig::load(
            FileBackedConfig::reader(defaults)?,
            FileBackedConfig::reader(global)?,
            FileBackedConfig::reader_from_ref(build)?,
            FileBackedConfig::reader(user)?,
        )?;
        Ok(FileBackedConfig { data })
    }

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

    pub fn save(
        &self,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<(), Error> {
        self.data.save(
            FileBackedConfig::writer(global)?,
            FileBackedConfig::writer_from_ref(build)?,
            FileBackedConfig::writer(user)?,
        )
    }
}

impl Config for FileBackedConfig {
    fn get(&self, key: &str) -> Option<Value> {
        self.data.get(key)
    }

    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
        self.data.set(level, key, value)
    }

    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<(), Error> {
        self.data.remove(level, key)
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    const ERROR: &'static str = "0";

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

    const DEFAULTS: &'static str = r#"
        {
            "name": "Defaults"
        }"#;

    #[test]
    fn test_priority_iterator() -> Result<(), Error> {
        let test = ConfigData {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let mut test_iter = test.iter();
        assert_eq!(test_iter.next(), Some(&test.user));
        assert_eq!(test_iter.next(), Some(&test.build));
        assert_eq!(test_iter.next(), Some(&test.global));
        assert_eq!(test_iter.next(), Some(&test.defaults));
        assert_eq!(test_iter.next(), None);
        Ok(())
    }

    #[test]
    fn test_priority_iterator_with_nones() -> Result<(), Error> {
        let test = ConfigData {
            user: Some(serde_json::from_str(USER)?),
            build: None,
            global: None,
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let mut test_iter = test.iter();
        assert_eq!(test_iter.next(), Some(&test.user));
        assert_eq!(test_iter.next(), Some(&test.build));
        assert_eq!(test_iter.next(), Some(&test.global));
        assert_eq!(test_iter.next(), Some(&test.defaults));
        assert_eq!(test_iter.next(), None);
        Ok(())
    }

    #[test]
    fn test_get() -> Result<(), Error> {
        let test = ConfigData {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value = test.get("name");
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("User")));

        let test_build = ConfigData {
            user: None,
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value_build = test_build.get("name");
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), Value::String(String::from("Build")));

        let test_global = ConfigData {
            user: None,
            build: None,
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value_global = test_global.get("name");
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), Value::String(String::from("Global")));

        let test_defaults = ConfigData {
            user: None,
            build: None,
            global: None,
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value_defaults = test_defaults.get("name");
        assert!(value_defaults.is_some());
        assert_eq!(value_defaults.unwrap(), Value::String(String::from("Defaults")));

        let test_none = ConfigData { user: None, build: None, global: None, defaults: None };

        let value_none = test_none.get("name");
        assert!(value_none.is_none());
        Ok(())
    }

    #[test]
    fn test_set_non_map_value() -> Result<(), Error> {
        let mut test = ConfigData {
            user: Some(serde_json::from_str(ERROR)?),
            build: None,
            global: None,
            defaults: None,
        };
        let value = test.set(&ConfigLevel::User, "name", Value::String(String::from("whatever")));
        assert!(value.is_err());
        Ok(())
    }

    #[test]
    fn test_get_nonexistent_config() -> Result<(), Error> {
        let test = ConfigData {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };
        let value = test.get("field that does not exist");
        assert!(value.is_none());
        Ok(())
    }

    #[test]
    fn test_set() -> Result<(), Error> {
        let mut test = ConfigData {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };
        test.set(&ConfigLevel::User, "name", Value::String(String::from("user-test")));
        let value = test.get("name");
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("user-test")));
        Ok(())
    }

    #[test]
    fn test_set_build_from_none() -> Result<(), Error> {
        let mut test = ConfigData { user: None, build: None, global: None, defaults: None };
        let value_none = test.get("name");
        assert!(value_none.is_none());
        test.set(&ConfigLevel::Defaults, "name", Value::String(String::from("defaults")));
        let value_defaults = test.get("name");
        assert!(value_defaults.is_some());
        assert_eq!(value_defaults.unwrap(), Value::String(String::from("defaults")));
        test.set(&ConfigLevel::Global, "name", Value::String(String::from("global")));
        let value_global = test.get("name");
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), Value::String(String::from("global")));
        test.set(&ConfigLevel::Build, "name", Value::String(String::from("build")));
        let value_build = test.get("name");
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), Value::String(String::from("build")));
        test.set(&ConfigLevel::User, "name", Value::String(String::from("user")));
        let value_user = test.get("name");
        assert!(value_user.is_some());
        assert_eq!(value_user.unwrap(), Value::String(String::from("user")));
        Ok(())
    }

    #[test]
    fn test_remove() -> Result<(), Error> {
        let mut test = ConfigData {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };
        test.remove(&ConfigLevel::User, "name")?;
        let user_value = test.get("name");
        assert!(user_value.is_some());
        assert_eq!(user_value.unwrap(), Value::String(String::from("Build")));
        test.remove(&ConfigLevel::Build, "name")?;
        let global_value = test.get("name");
        assert!(global_value.is_some());
        assert_eq!(global_value.unwrap(), Value::String(String::from("Global")));
        test.remove(&ConfigLevel::Global, "name")?;
        let default_value = test.get("name");
        assert!(default_value.is_some());
        assert_eq!(default_value.unwrap(), Value::String(String::from("Defaults")));
        test.remove(&ConfigLevel::Defaults, "name")?;
        let none_value = test.get("name");
        assert!(none_value.is_none());
        Ok(())
    }

    #[test]
    fn test_persistent_build() -> Result<(), Error> {
        let mut user_file = String::from(USER);
        let mut build_file = String::from(BUILD);
        let mut global_file = String::from(GLOBAL);
        let defaults_file = String::from(DEFAULTS);

        let persistent_config = PersistentConfig::load(
            Some(BufReader::new(defaults_file.as_bytes())),
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

    #[test]
    fn test_heuristic_build() -> Result<(), Error> {
        let mut user_file = String::from(USER);
        let mut build_file = String::from(BUILD);
        let mut global_file = String::from(GLOBAL);
        let defaults_file = String::from(DEFAULTS);

        let mut heuristic_config = HeuristicConfig::load(
            Some(BufReader::new(defaults_file.as_bytes())),
            Some(BufReader::new(global_file.as_bytes())),
            Some(BufReader::new(build_file.as_bytes())),
            Some(BufReader::new(user_file.as_bytes())),
        )?;

        let value = heuristic_config.get("name");
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("User")));

        let missing_value = heuristic_config.get("ssh-keys");
        assert!(missing_value.is_none());

        heuristic_config
            .heuristics
            .insert("ssh-keys", |_| Some(Value::String(String::from("test ssh keys"))));

        let not_missing_value = heuristic_config.get("ssh-keys");
        assert!(not_missing_value.is_some());
        assert_eq!(not_missing_value.unwrap(), Value::String(String::from("test ssh keys")));

        let mut user_file_out = String::new();
        let mut build_file_out = String::new();
        let mut global_file_out = String::new();

        unsafe {
            heuristic_config.save(
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
