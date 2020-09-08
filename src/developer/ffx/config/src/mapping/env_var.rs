// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mapping::{postprocess, preprocess, replace},
    anyhow::anyhow,
    lazy_static::lazy_static,
    regex::Regex,
    serde_json::Value,
    std::env,
};

fn check(value: &String, regex: &Regex) -> bool {
    // First verify all environment variables exist.
    // If one does not exist, return none.
    for caps in regex.captures_iter(&value) {
        // Skip the first one since that'll be the whole string.
        for cap in caps.iter().skip(1) {
            if let Some(c) = cap {
                let var = c.as_str();
                if let Err(_) = env::var(var) {
                    return false;
                }
            }
        }
    }
    true
}

pub(crate) fn env_var<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
    lazy_static! {
        static ref REGEX: Regex = Regex::new(r"\$([A-Z][A-Z0-9_]*)").unwrap();
    }

    Box::new(move |value| -> Option<Value> {
        let env_string = preprocess(&value);
        if let Some(ref e) = env_string {
            if !check(e, &*REGEX) {
                return None;
            }
        }
        match env_string
            .as_ref()
            .map(|s| replace(s, &*REGEX, |v| env::var(v).map_err(|_| anyhow!(""))))
            .map(postprocess)
        {
            Some(v) => next(v),
            None => next(value),
        }
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::mapping::identity::identity;

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
