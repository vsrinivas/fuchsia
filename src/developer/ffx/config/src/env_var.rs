// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::HOME,
    lazy_static::lazy_static,
    regex::{Captures, Regex},
    serde_json::Value,
    std::env,
};

// Negative lookbehind (or lookahead for that matter) is not supported in Rust's regex.
// Instead, replace with this string - which hopefully will not be used by anyone in the
// configuration.  Insert joke here about how hope is not a strategy.
const TEMP_REPLACE: &str = "#<#ffx!!replace#>#";

pub(crate) fn env_var<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
    lazy_static! {
        static ref ENV_VAR_REGEX: Regex = Regex::new(r"\$([A-Z][A-Z0-9_]*)").unwrap();
    }

    Box::new(move |value| -> Option<Value> {
        let env_string =
            value.as_str().map(|s| s.to_string()).map(|s| s.replace("$$", TEMP_REPLACE));
        if let Some(estring) = env_string.clone() {
            // First verify all environment variables exist.
            // If one does not exist, return none.
            for caps in ENV_VAR_REGEX.captures_iter(&estring) {
                // Skip the first one since that'll be the whole string.
                for cap in caps.iter().skip(1) {
                    if let Some(c) = cap {
                        let var = c.as_str();
                        // HOME is a special variable that will use the home crate.
                        if var != HOME {
                            if let Err(_) = env::var(var) {
                                return None;
                            }
                        }
                    }
                }
            }
        }

        env_string
            .as_ref()
            .map(|s| {
                ENV_VAR_REGEX.replace_all(s, |caps: &Captures<'_>| {
                    // Skip the first one since that'll be the whole string.
                    caps.iter()
                        .skip(1)
                        .map(|cap| {
                            cap.map(|c| {
                                let var = c.as_str();
                                // HOME is a special variable that will use the home crate.
                                if var == HOME {
                                    let home_path =
                                        home::home_dir().expect("unknown home directory");
                                    home_path.to_str().map(|s| s.to_string()).ok_or(
                                        env::VarError::NotUnicode(home_path.into_os_string()),
                                    )
                                } else {
                                    env::var(var)
                                }
                            })
                        })
                        .fold(
                            String::new(),
                            |acc, v| if let Some(Ok(s)) = v { acc + &s } else { acc },
                        )
                })
            })
            .map(|r| Value::String(r.to_string().replace(TEMP_REPLACE, "$")))
            .and_then(next)
            .or(next(value))
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::identity::identity;

    fn setup_test(env_vars: Vec<(&'static str, &'static str)>) -> Box<dyn FnOnce() -> ()> {
        env_vars.iter().for_each(|(var, val)| env::set_var(var, val));
        Box::new(move || {
            env_vars.iter().for_each(|(var, _)| env::remove_var(var));
        })
    }

    #[test]
    fn test_env_var_mapper() {
        let cleanup: Box<dyn FnOnce() -> ()> =
            setup_test(vec![("FFX_TEST_ENV_VAR_MAPPER", "test")]);
        let test = Value::String("$FFX_TEST_ENV_VAR_MAPPER".to_string());
        assert_eq!(env_var(&identity)(test), Some(Value::String("test".to_string())));
        cleanup();
    }

    #[test]
    fn test_env_var_mapper_multiple() {
        let cleanup: Box<dyn FnOnce() -> ()> =
            setup_test(vec![("FFX_TEST_ENV_VAR_MAPPER_MULTIPLE", "test")]);
        let test = Value::String(
            "$FFX_TEST_ENV_VAR_MAPPER_MULTIPLE/$FFX_TEST_ENV_VAR_MAPPER_MULTIPLE".to_string(),
        );
        assert_eq!(env_var(&identity)(test), Some(Value::String(format!("{}/{}", "test", "test"))));
        cleanup();
    }

    #[test]
    fn test_env_var_mapper_returns_none() {
        let test = Value::String("$ENVIRONMENT_VARIABLE_THAT_DOES_NOT_EXIST".to_string());
        assert_eq!(env_var(&identity)(test), None);
    }

    #[test]
    fn test_env_var_mapper_multiple_returns_none_if_one_does_not_exist() {
        let cleanup: Box<dyn FnOnce() -> ()> =
            setup_test(vec![("FFX_TEST_ENV_VAR_EXISTS", "test")]);
        let test = Value::String("$HOME/$ENVIRONMENT_VARIABLE_THAT_DOES_NOT_EXIST".to_string());
        assert_eq!(env_var(&identity)(test), None);
        cleanup();
    }

    #[test]
    fn test_env_var_mapper_escapes_dollar_sign() {
        let test = Value::String("$$HOME".to_string());
        assert_eq!(env_var(&identity)(test), Some(Value::String("$HOME".to_string())));
    }

    #[test]
    fn test_env_var_returns_value_if_not_string() {
        let test = Value::Bool(false);
        assert_eq!(env_var(&identity)(test), Some(Value::Bool(false)));
    }
}
