// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    serde_json::Value,
    std::fs::{File, OpenOptions},
    std::io,
    std::path::Path,
};

enum ConfigLevel {
    Defaults,
    Build,
    Global,
    User,
}

trait Config<'a> {
    fn get(&'a self, key: &str) -> Result<Option<&'a Value>, Error>;
    fn set(&'a mut self, level: ConfigLevel, key: &str, value: Value) -> Result<(), Error>;
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

impl<'a> ConfigData {
    fn iter(&'a self) -> PriorityConfigIterator {
        PriorityConfigIterator { curr: None, config: self }
    }
}

impl<'a> Config<'a> for ConfigData {
    fn get(&'a self, key: &str) -> Result<Option<&'a Value>, Error> {
        Ok(self
            .iter()
            .filter(|c| c.is_some())
            .filter_map(|c| c.as_ref().unwrap().as_object())
            .find_map(|c| c.get(key)))
    }

    fn set(&'a mut self, level: ConfigLevel, key: &str, value: Value) -> Result<(), Error> {
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

        let value = test.get("name")?;
        assert!(value.is_some());
        assert_eq!(value.unwrap(), &Value::String(String::from("User")));

        let test_build = ConfigData {
            user: None,
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value_build = test_build.get("name")?;
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), &Value::String(String::from("Build")));

        let test_global = ConfigData {
            user: None,
            build: None,
            global: Some(serde_json::from_str(GLOBAL)?),
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value_global = test_global.get("name")?;
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), &Value::String(String::from("Global")));

        let test_defaults = ConfigData {
            user: None,
            build: None,
            global: None,
            defaults: Some(serde_json::from_str(DEFAULTS)?),
        };

        let value_defaults = test_defaults.get("name")?;
        assert!(value_defaults.is_some());
        assert_eq!(value_defaults.unwrap(), &Value::String(String::from("Defaults")));

        let test_none = ConfigData { user: None, build: None, global: None, defaults: None };

        let value_none = test_none.get("name")?;
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
        let value = test.set(ConfigLevel::User, "name", Value::String(String::from("whatever")));
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
        let value = test.get("field that does not exist")?;
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
        test.set(ConfigLevel::User, "name", Value::String(String::from("user-test")));
        let value = test.get("name")?;
        assert!(value.is_some());
        assert_eq!(value.unwrap(), &Value::String(String::from("user-test")));
        Ok(())
    }

    #[test]
    fn test_set_build_from_none() -> Result<(), Error> {
        let mut test = ConfigData { user: None, build: None, global: None, defaults: None };
        let value_none = test.get("name")?;
        assert!(value_none.is_none());
        test.set(ConfigLevel::Defaults, "name", Value::String(String::from("defaults")));
        let value_defaults = test.get("name")?;
        assert!(value_defaults.is_some());
        assert_eq!(value_defaults.unwrap(), &Value::String(String::from("defaults")));
        test.set(ConfigLevel::Global, "name", Value::String(String::from("global")));
        let value_global = test.get("name")?;
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), &Value::String(String::from("global")));
        test.set(ConfigLevel::Build, "name", Value::String(String::from("build")));
        let value_build = test.get("name")?;
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), &Value::String(String::from("build")));
        test.set(ConfigLevel::User, "name", Value::String(String::from("user")));
        let value_user = test.get("name")?;
        assert!(value_user.is_some());
        assert_eq!(value_user.unwrap(), &Value::String(String::from("user")));
        Ok(())
    }
}
