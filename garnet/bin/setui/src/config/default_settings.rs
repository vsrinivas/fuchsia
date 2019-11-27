// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog::fx_log_err;
use serde::de::DeserializeOwned;
use std::fs::File;
use std::io::BufReader;

pub struct DefaultSetting<T: DeserializeOwned + Clone> {
    default_value: T,
    config_file_path: Option<String>,
    cached_value: Option<T>,
}

impl<T: DeserializeOwned + Clone> DefaultSetting<T> {
    pub fn new(default_value: T, config_file_path: Option<String>) -> Self {
        DefaultSetting {
            default_value: default_value,
            config_file_path: config_file_path,
            cached_value: None,
        }
    }

    pub fn get_default_value(&mut self) -> T {
        if self.cached_value.is_none() {
            self.cached_value = Some(self.load_default_settings());
        }

        self.cached_value.as_ref().unwrap().clone()
    }

    fn load_default_settings(&self) -> T {
        if self.config_file_path.is_none() {
            return self.default_value.clone();
        }

        let file_path = self.config_file_path.as_ref().unwrap();
        let file = match File::open(file_path.clone()) {
            Ok(f) => f,
            Err(e) => {
                fx_log_err!("unable to open {}, using defaults: {:?}", file_path, e);
                return self.default_value.clone();
            }
        };

        match serde_json::from_reader(BufReader::new(file)) {
            Ok(value) => value,
            Err(e) => {
                fx_log_err!("unable to parse config, using defaults: {:?}", e);
                self.default_value.clone()
            }
        }
    }
}

#[cfg(test)]
pub mod testing {
    use super::*;
    use serde_derive::Deserialize;

    #[derive(Clone, Deserialize)]
    struct TestConfigData {
        value: u32,
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_valid_config_data() {
        let mut setting = DefaultSetting::new(
            TestConfigData { value: 3 },
            Some("/config/data/fake_config_data.json".to_string()),
        );

        assert_eq!(setting.get_default_value().value, 10);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_invalid_config_data() {
        let mut setting = DefaultSetting::new(
            TestConfigData { value: 3 },
            Some("/config/data/fake_invalid_config_data.json".to_string()),
        );

        assert_eq!(setting.get_default_value().value, 3);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_invalid_config_file_path() {
        let mut setting =
            DefaultSetting::new(TestConfigData { value: 3 }, Some("nuthatch".to_string()));

        assert_eq!(setting.get_default_value().value, 3);
    }
}
