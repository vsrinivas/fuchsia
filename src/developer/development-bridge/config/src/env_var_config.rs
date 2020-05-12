// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::api::ReadConfig, serde_json::Value, std::collections::HashMap};

pub(crate) struct EnvironmentVariable<'a> {
    environment_variables: &'a HashMap<&'static str, Vec<&'static str>>,
}

impl<'a> EnvironmentVariable<'a> {
    pub(crate) fn new(environment_variables: &'a HashMap<&'static str, Vec<&'static str>>) -> Self {
        Self { environment_variables }
    }
}

impl ReadConfig for EnvironmentVariable<'_> {
    fn get(&self, key: &str) -> Option<Value> {
        match self.environment_variables.get(key) {
            Some(vars) => vars
                .iter()
                .map(|var| std::env::var(var).map_or(None, |v| Some(Value::String(v))))
                .find(|val| val.is_some())
                .flatten(),
            None => None,
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_environment_variables() {
        let (env_key, env_key_2) = ("test", "test_2");
        let (env_var_1, env_var_1_value) = ("FFX_ENV_TEST_1", "test 1");
        let (env_var_2, env_var_2_value) = ("FFX_ENV_TEST_2", "test 2");
        let (env_var_3, env_var_3_value) = ("FFX_ENV_TEST_3", "test 3");
        let (env_var_4, env_var_4_value) = ("FFX_ENV_TEST_4", "test 4");
        vec![env_var_1, env_var_2, env_var_3, env_var_4].iter().for_each(std::env::remove_var);

        let mut environment_variables = HashMap::<&str, Vec<&str>>::new();
        environment_variables.insert(env_key, vec![env_var_1, env_var_2, env_var_3]);
        environment_variables.insert(env_key_2, vec![env_var_4]);

        let config = EnvironmentVariable::new(&environment_variables);

        let missing_key = "whatever";
        assert_eq!(None, config.get(missing_key));
        assert_eq!(None, config.get(env_key));
        assert_eq!(None, config.get(env_key_2));

        std::env::set_var(env_var_4, env_var_4_value);
        assert_eq!(Some(Value::String(env_var_4_value.to_string())), config.get(env_key_2));

        std::env::set_var(env_var_3, env_var_3_value);
        assert_eq!(Some(Value::String(env_var_3_value.to_string())), config.get(env_key));
        std::env::set_var(env_var_2, env_var_2_value);
        assert_eq!(Some(Value::String(env_var_2_value.to_string())), config.get(env_key));
        std::env::set_var(env_var_1, env_var_1_value);
        assert_eq!(Some(Value::String(env_var_1_value.to_string())), config.get(env_key));
    }
}
