// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::query::SelectMode,
    crate::{ConfigLevel, ConfigQuery},
    anyhow::{anyhow, bail, Result},
    config_macros::include_default,
    serde_json::{Map, Value},
    std::fmt,
};

pub(crate) struct Priority {
    default: Option<Value>,
    pub(crate) build: Option<Value>,
    pub(crate) global: Option<Value>,
    pub(crate) user: Option<Value>,
    pub(crate) runtime: Option<Value>,
}

struct PriorityIterator<'a> {
    curr: Option<ConfigLevel>,
    config: &'a Priority,
}

impl<'a> Iterator for PriorityIterator<'a> {
    type Item = &'a Option<Value>;

    fn next(&mut self) -> Option<Self::Item> {
        match &self.curr {
            None => {
                self.curr = Some(ConfigLevel::Runtime);
                Some(&self.config.runtime)
            }
            Some(level) => match level {
                ConfigLevel::Runtime => {
                    self.curr = Some(ConfigLevel::User);
                    Some(&self.config.user)
                }
                ConfigLevel::User => {
                    self.curr = Some(ConfigLevel::Build);
                    Some(&self.config.build)
                }
                ConfigLevel::Build => {
                    self.curr = Some(ConfigLevel::Global);
                    Some(&self.config.global)
                }
                ConfigLevel::Global => {
                    self.curr = Some(ConfigLevel::Default);
                    Some(&self.config.default)
                }
                ConfigLevel::Default => None,
            },
        }
    }
}

impl Priority {
    pub(crate) fn new(
        user: Option<Value>,
        build: Option<Value>,
        global: Option<Value>,
        runtime: Option<Value>,
    ) -> Self {
        Self { user, build, global, runtime, default: include_default!() }
    }

    fn iter(&self) -> PriorityIterator<'_> {
        PriorityIterator { curr: None, config: self }
    }

    pub fn nested_map<T: Fn(Value) -> Option<Value>>(
        cur: Option<Value>,
        mapper: &T,
    ) -> Option<Value> {
        cur.and_then(|c| {
            if let Value::Object(map) = c {
                let mut result = Map::new();
                for (key, value) in map.iter() {
                    let new_value = if value.is_object() {
                        Priority::nested_map(map.get(key).cloned(), mapper)
                    } else {
                        map.get(key).cloned().and_then(|v| mapper(v))
                    };
                    if let Some(new_value) = new_value {
                        result.insert(key.to_string(), new_value);
                    }
                }
                if result.len() == 0 {
                    None
                } else {
                    Some(Value::Object(result))
                }
            } else {
                mapper(c)
            }
        })
    }

    fn nested_get<T: Fn(Value) -> Option<Value>>(
        cur: &Option<Value>,
        key: &str,
        remaining_keys: Vec<&str>,
        mapper: &T,
    ) -> Option<Value> {
        cur.as_ref().and_then(|c| {
            if remaining_keys.len() == 0 {
                Priority::nested_map(c.get(key).cloned(), mapper)
            } else {
                Priority::nested_get(
                    &c.get(key).cloned(),
                    remaining_keys[0],
                    remaining_keys[1..].to_vec(),
                    mapper,
                )
            }
        })
    }

    pub(crate) fn nested_set(
        cur: &mut Map<String, Value>,
        key: &str,
        remaining_keys: Vec<&str>,
        value: Value,
    ) {
        if remaining_keys.len() == 0 {
            cur.insert(key.to_string(), value);
        } else {
            match cur.get(key) {
                Some(value) => {
                    if !value.is_object() {
                        // Any literals will be overridden.
                        cur.insert(key.to_string(), Value::Object(Map::new()));
                    }
                }
                None => {
                    cur.insert(key.to_string(), Value::Object(Map::new()));
                }
            }
            // Just ensured this would be the case.
            let next_map = cur
                .get_mut(key)
                .expect("unable to get configuration")
                .as_object_mut()
                .expect("Unable to set configuration value as map");
            Priority::nested_set(next_map, remaining_keys[0], remaining_keys[1..].to_vec(), value);
        }
    }

    fn nested_remove(
        cur: &mut Map<String, Value>,
        key: &str,
        remaining_keys: Vec<&str>,
    ) -> Result<()> {
        if remaining_keys.len() == 0 {
            cur.remove(&key.to_string()).ok_or(anyhow!("Config key not found")).map(|_| ())
        } else {
            match cur.get(key) {
                Some(value) => {
                    if !value.is_object() {
                        bail!("Configuration literal found when expecting a map.")
                    }
                }
                None => {
                    bail!("Configuration key not found.");
                }
            }
            // Just ensured this would be the case.
            let next_map = cur
                .get_mut(key)
                .expect("unable to get configuration")
                .as_object_mut()
                .expect("Unable to set configuration value as map");
            Priority::nested_remove(next_map, remaining_keys[0], remaining_keys[1..].to_vec())
        }
    }

    fn get_level_map(&mut self, level: &ConfigLevel) -> &mut Map<String, Value> {
        let config = match level {
            ConfigLevel::Runtime => &mut self.runtime,
            ConfigLevel::User => &mut self.user,
            ConfigLevel::Build => &mut self.build,
            ConfigLevel::Global => &mut self.global,
            ConfigLevel::Default => &mut self.default,
        };
        // Ensure current value is always a map.
        match config {
            Some(v) => {
                if !v.is_object() {
                    // This must be a map.  Will override any literals or arrays.
                    *config = Some(Value::Object(Map::new()));
                }
            }
            None => *config = Some(Value::Object(Map::new())),
        }
        // Ok to expect as this is ensured above.
        config
            .as_mut()
            .expect("uninitialzed configuration")
            .as_object_mut()
            .expect("unable to initialize configuration map")
    }

    pub fn get<T: Fn(Value) -> Option<Value>>(
        &self,
        key: &ConfigQuery<'_>,
        mapper: &T,
    ) -> Option<Value> {
        if let Some(name) = key.name {
            // Check for nested config values if there's a '.' in the key
            let key_vec: Vec<&str> = name.split('.').collect();
            if let Some(level) = key.level {
                let config = match level {
                    ConfigLevel::Runtime => &self.runtime,
                    ConfigLevel::User => &self.user,
                    ConfigLevel::Build => &self.build,
                    ConfigLevel::Global => &self.global,
                    ConfigLevel::Default => &self.default,
                };
                Priority::nested_get(config, key_vec[0], key_vec[1..].to_vec(), mapper)
            } else {
                match key.select {
                    SelectMode::First => self.iter().find_map(|c| {
                        Priority::nested_get(c, key_vec[0], key_vec[1..].to_vec(), mapper)
                    }),
                    SelectMode::All => {
                        let result: Vec<Value> = self
                            .iter()
                            .filter_map(|c| {
                                Priority::nested_get(c, key_vec[0], key_vec[1..].to_vec(), mapper)
                            })
                            .collect();
                        if result.len() > 0 {
                            Some(Value::Array(result))
                        } else {
                            None
                        }
                    }
                }
            }
        } else {
            if let Some(level) = key.level {
                let config = match level {
                    ConfigLevel::Runtime => &self.runtime,
                    ConfigLevel::User => &self.user,
                    ConfigLevel::Build => &self.build,
                    ConfigLevel::Global => &self.global,
                    ConfigLevel::Default => &self.default,
                };
                Priority::nested_map(config.clone(), mapper)
            } else {
                // Not really supported now.  Maybe in the future.
                None
            }
        }
    }

    pub fn set(&mut self, query: &ConfigQuery<'_>, value: Value) -> Result<()> {
        let key = if let Some(k) = query.name {
            k
        } else {
            bail!("name of configuration is required to set a value");
        };

        let level = if let Some(l) = query.level {
            l
        } else {
            bail!("level of configuration is required to set a value");
        };

        let key_vec: Vec<&str> = key.split('.').collect();
        Priority::nested_set(
            &mut self.get_level_map(&level),
            key_vec[0],
            key_vec[1..].to_vec(),
            value,
        );
        Ok(())
    }

    pub fn remove(&mut self, query: &ConfigQuery<'_>) -> Result<()> {
        let key = if let Some(k) = query.name {
            k
        } else {
            bail!("name of configuration is required to remove a value");
        };

        let level = if let Some(l) = query.level {
            l
        } else {
            bail!("level of configuration is required to remove a value");
        };

        let key_vec: Vec<&str> = key.split('.').collect();
        Priority::nested_remove(&mut self.get_level_map(&level), key_vec[0], key_vec[1..].to_vec())
    }
}

impl fmt::Display for Priority {
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
    use crate::mapping::identity::identity;
    use regex::Regex;
    use serde_json::json;

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

    const DEFAULT: &'static str = r#"
        {
            "name": "Default"
        }"#;

    const RUNTIME: &'static str = r#"
        {
            "name": "Runtime"
        }"#;

    const MAPPED: &'static str = r#"
        {
            "name": "TEST_MAP"
        }"#;

    const NESTED: &'static str = r#"
        {
            "name": {
               "nested": "Nested"
            }
        }"#;

    const DEEP: &'static str = r#"
        {
            "name": {
               "nested": {
                    "deep": {
                        "name": "TEST_MAP"
                    }
               }
            }
        }"#;

    #[test]
    fn test_priority_iterator() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: Some(serde_json::from_str(RUNTIME)?),
        };

        let mut test_iter = test.iter();
        assert_eq!(test_iter.next(), Some(&test.runtime));
        assert_eq!(test_iter.next(), Some(&test.user));
        assert_eq!(test_iter.next(), Some(&test.build));
        assert_eq!(test_iter.next(), Some(&test.global));
        assert_eq!(test_iter.next(), Some(&test.default));
        Ok(())
    }

    #[test]
    fn test_priority_iterator_with_nones() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: None,
            global: None,
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };

        let mut test_iter = test.iter();
        assert_eq!(test_iter.next(), Some(&test.runtime));
        assert_eq!(test_iter.next(), Some(&test.user));
        assert_eq!(test_iter.next(), Some(&test.build));
        assert_eq!(test_iter.next(), Some(&test.global));
        assert_eq!(test_iter.next(), Some(&test.default));
        Ok(())
    }

    #[test]
    fn test_get() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };

        let value = test.get(&"name".into(), &identity);
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("User")));

        let test_build = Priority {
            user: None,
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };

        let value_build = test_build.get(&"name".into(), &identity);
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), Value::String(String::from("Build")));

        let test_global = Priority {
            user: None,
            build: None,
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };

        let value_global = test_global.get(&"name".into(), &identity);
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), Value::String(String::from("Global")));

        let test_default = Priority {
            user: None,
            build: None,
            global: None,
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };

        let value_default = test_default.get(&"name".into(), &identity);
        assert!(value_default.is_some());
        assert_eq!(value_default.unwrap(), Value::String(String::from("Default")));

        let test_none =
            Priority { user: None, build: None, global: None, default: None, runtime: None };

        let value_none = test_none.get(&"name".into(), &identity);
        assert!(value_none.is_none());
        Ok(())
    }

    #[test]
    fn test_set_non_map_value() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(ERROR)?),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        test.set(&("name", &ConfigLevel::User).into(), Value::String(String::from("whatever")))?;
        let value = test.get(&"name".into(), &identity);
        assert_eq!(value, Some(Value::String(String::from("whatever"))));
        Ok(())
    }

    #[test]
    fn test_get_nonexistent_config() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };
        let value = test.get(&"field that does not exist".into(), &identity);
        assert!(value.is_none());
        Ok(())
    }

    #[test]
    fn test_set() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };
        test.set(&("name", &ConfigLevel::User).into(), Value::String(String::from("user-test")))?;
        let value = test.get(&"name".into(), &identity);
        assert!(value.is_some());
        assert_eq!(value.unwrap(), Value::String(String::from("user-test")));
        Ok(())
    }

    #[test]
    fn test_set_build_from_none() -> Result<()> {
        let mut test =
            Priority { user: None, build: None, global: None, default: None, runtime: None };
        let value_none = test.get(&"name".into(), &identity);
        assert!(value_none.is_none());
        test.set(&("name", &ConfigLevel::Default).into(), Value::String(String::from("default")))?;
        let value_default = test.get(&"name".into(), &identity);
        assert!(value_default.is_some());
        assert_eq!(value_default.unwrap(), Value::String(String::from("default")));
        test.set(&("name", &ConfigLevel::Global).into(), Value::String(String::from("global")))?;
        let value_global = test.get(&"name".into(), &identity);
        assert!(value_global.is_some());
        assert_eq!(value_global.unwrap(), Value::String(String::from("global")));
        test.set(&("name", &ConfigLevel::Build).into(), Value::String(String::from("build")))?;
        let value_build = test.get(&"name".into(), &identity);
        assert!(value_build.is_some());
        assert_eq!(value_build.unwrap(), Value::String(String::from("build")));
        test.set(&("name", &ConfigLevel::User).into(), Value::String(String::from("user")))?;
        let value_user = test.get(&"name".into(), &identity);
        assert!(value_user.is_some());
        assert_eq!(value_user.unwrap(), Value::String(String::from("user")));
        Ok(())
    }

    #[test]
    fn test_remove() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
        };
        test.remove(&("name", &ConfigLevel::User).into())?;
        let user_value = test.get(&"name".into(), &identity);
        assert!(user_value.is_some());
        assert_eq!(user_value.unwrap(), Value::String(String::from("Build")));
        test.remove(&("name", &ConfigLevel::Build).into())?;
        let global_value = test.get(&"name".into(), &identity);
        assert!(global_value.is_some());
        assert_eq!(global_value.unwrap(), Value::String(String::from("Global")));
        test.remove(&("name", &ConfigLevel::Global).into())?;
        let default_value = test.get(&"name".into(), &identity);
        assert!(default_value.is_some());
        assert_eq!(default_value.unwrap(), Value::String(String::from("Default")));
        test.remove(&("name", &ConfigLevel::Default).into())?;
        let none_value = test.get(&"name".into(), &identity);
        assert!(none_value.is_none());
        Ok(())
    }

    #[test]
    fn test_default() {
        let test = Priority::new(None, None, None, None);
        let default_value = test.get(&"log.enabled".into(), &identity);
        assert_eq!(default_value.unwrap(), Value::String("$FFX_LOG_ENABLED".to_string()));
    }

    #[test]
    fn test_display() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: None,
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

    fn test_map(value: Value) -> Option<Value> {
        if value == "TEST_MAP".to_string() {
            Some(Value::String("passed".to_string()))
        } else {
            Some(Value::String("failed".to_string()))
        }
    }

    #[test]
    fn test_mapping() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(MAPPED)?),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        let test_mapping = "TEST_MAP".to_string();
        let test_passed = "passed".to_string();
        let mapped_value = test.get(&"name".into(), &test_map);
        assert_eq!(mapped_value, Some(Value::String(test_passed)));
        let identity_value = test.get(&"name".into(), &identity);
        assert_eq!(identity_value, Some(Value::String(test_mapping)));
        Ok(())
    }

    #[test]
    fn test_nested_get() -> Result<()> {
        let test = Priority {
            user: None,
            build: None,
            global: None,
            default: None,
            runtime: Some(serde_json::from_str(NESTED)?),
        };
        let value = test.get(&"name.nested".into(), &identity);
        assert_eq!(value, Some(Value::String("Nested".to_string())));
        Ok(())
    }

    #[test]
    fn test_nested_get_should_return_sub_tree() -> Result<()> {
        let test = Priority {
            user: None,
            build: None,
            global: None,
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: Some(serde_json::from_str(NESTED)?),
        };
        let value = test.get(&"name".into(), &identity);
        assert_eq!(value, Some(serde_json::from_str("{\"nested\": \"Nested\"}")?));
        Ok(())
    }

    #[test]
    fn test_nested_get_should_return_full_match() -> Result<()> {
        let test = Priority {
            user: None,
            build: None,
            global: None,
            default: Some(serde_json::from_str(NESTED)?),
            runtime: Some(serde_json::from_str(RUNTIME)?),
        };
        let value = test.get(&"name.nested".into(), &identity);
        assert_eq!(value, Some(Value::String("Nested".to_string())));
        Ok(())
    }

    #[test]
    fn test_nested_get_should_map_values_in_sub_tree() -> Result<()> {
        let test = Priority {
            user: None,
            build: None,
            global: None,
            default: Some(serde_json::from_str(NESTED)?),
            runtime: Some(serde_json::from_str(DEEP)?),
        };
        let value = test.get(&"name.nested".into(), &test_map);
        assert_eq!(value, Some(serde_json::from_str("{\"deep\": {\"name\": \"passed\"}}")?));
        Ok(())
    }

    #[test]
    fn test_nested_set_from_none() -> Result<()> {
        let mut test =
            Priority { user: None, build: None, global: None, default: None, runtime: None };
        test.set(&("name.nested", &ConfigLevel::User).into(), Value::Bool(false))?;
        let nested_value = test.get(&"name".into(), &identity);
        assert_eq!(nested_value, Some(serde_json::from_str("{\"nested\": false}")?));
        Ok(())
    }

    #[test]
    fn test_nested_set_from_already_populated_tree() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(NESTED)?),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        test.set(&("name.updated", &ConfigLevel::User).into(), Value::Bool(true))?;
        let expected = json!({
           "nested": "Nested",
           "updated": true
        });
        let nested_value = test.get(&"name".into(), &identity);
        assert_eq!(nested_value, Some(expected));
        Ok(())
    }

    #[test]
    fn test_nested_set_override_literals() -> Result<()> {
        let mut test = Priority {
            user: Some(json!([])),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        test.set(&("name.updated", &ConfigLevel::User).into(), Value::Bool(true))?;
        let expected = json!({
           "updated": true
        });
        let nested_value = test.get(&"name".into(), &identity);
        assert_eq!(nested_value, Some(expected));
        test.set(&("name.updated", &ConfigLevel::User).into(), serde_json::from_str(NESTED)?)?;
        let nested_value = test.get(&"name.updated.name.nested".into(), &identity);
        assert_eq!(nested_value, Some(Value::String(String::from("Nested"))));
        Ok(())
    }

    #[test]
    fn test_nested_remove_from_none() -> Result<()> {
        let mut test =
            Priority { user: None, build: None, global: None, default: None, runtime: None };
        let result = test.remove(&("name.nested", &ConfigLevel::User).into());
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_nested_remove_throws_error_if_key_not_found() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(NESTED)?),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        let result = test.remove(&("name.unknown", &ConfigLevel::User).into());
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_nested_remove_deletes_literals() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(DEEP)?),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        test.remove(&("name.nested.deep.name", &ConfigLevel::User).into())?;
        let value = test.get(&"name".into(), &identity);
        assert_eq!(value, None);
        Ok(())
    }

    #[test]
    fn test_nested_remove_deletes_subtrees() -> Result<()> {
        let mut test = Priority {
            user: Some(serde_json::from_str(DEEP)?),
            build: None,
            global: None,
            default: None,
            runtime: None,
        };
        test.remove(&("name.nested", &ConfigLevel::User).into())?;
        let value = test.get(&"name".into(), &identity);
        assert_eq!(value, None);
        Ok(())
    }

    #[test]
    fn test_additive_mode() -> Result<()> {
        let test = Priority {
            user: Some(serde_json::from_str(USER)?),
            build: Some(serde_json::from_str(BUILD)?),
            global: Some(serde_json::from_str(GLOBAL)?),
            default: Some(serde_json::from_str(DEFAULT)?),
            runtime: Some(serde_json::from_str(RUNTIME)?),
        };
        let value = test.get(&("name", &SelectMode::All).into(), &identity);
        match value {
            Some(Value::Array(v)) => {
                assert_eq!(v.len(), 5);
                assert_eq!(v[0], Value::String("Runtime".to_string()));
                assert_eq!(v[1], Value::String("User".to_string()));
                assert_eq!(v[2], Value::String("Build".to_string()));
                assert_eq!(v[3], Value::String("Global".to_string()));
                assert_eq!(v[4], Value::String("Default".to_string()));
            }
            _ => bail!("additive mode should return a Value::Array full of all values."),
        }
        Ok(())
    }
}
