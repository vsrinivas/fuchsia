// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::de::DeserializeOwned;
use std::fmt::Display;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

use crate::service::message::Messenger;

pub struct DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone,
    P: AsRef<Path> + Display,
{
    default_value: Option<T>,
    config_file_path: P,
    cached_value: Option<Option<T>>,
    _messenger: Option<Messenger>,
    // Whether to log the config loads into inspect.
    _log_config_loads: bool,
}

impl<T, P> DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone,
    P: AsRef<Path> + Display,
{
    pub fn new(
        default_value: Option<T>,
        config_file_path: P,
        messenger: Option<Messenger>,
        log_config_loads: bool,
    ) -> Self {
        DefaultSetting {
            default_value,
            config_file_path,
            cached_value: None,
            _messenger: messenger,
            _log_config_loads: log_config_loads,
        }
    }

    pub fn get_default_value(&mut self) -> Option<T> {
        if self.cached_value.is_none() {
            self.cached_value = Some(self.load_default_settings());
        }

        self.cached_value.as_ref().unwrap().clone()
    }

    /// Attempts to load the settings from the given config_file_path.
    ///
    /// Returns the default value if unable to read or parse the file. The returned option will
    /// only be None if the default_value was provided as None.
    fn load_default_settings(&self) -> Option<T> {
        File::open(self.config_file_path.as_ref())
            .map_err(|_e| {
                // TODO(fxbug.dev/67569): Report config load status.
            })
            .ok()
            .and_then(|file| {
                serde_json::from_reader(BufReader::new(file))
                    .map_err(|_e| {
                        // TODO(fxbug.dev/67569): Report config load status.
                    })
                    .ok()
            })
            .unwrap_or_else(|| self.default_value.clone())
    }
}

#[cfg(test)]
pub mod testing {
    use super::*;
    use serde::Deserialize;

    #[derive(Clone, Deserialize)]
    struct TestConfigData {
        value: u32,
    }

    #[test]
    fn test_load_valid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_config_data.json",
            None,
            true,
        );

        assert_eq!(setting.get_default_value().unwrap().value, 10);
    }

    #[test]
    fn test_load_invalid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_invalid_config_data.json",
            None,
            true,
        );

        assert_eq!(setting.get_default_value().unwrap().value, 3);
    }

    #[test]
    fn test_load_invalid_config_file_path() {
        let mut setting =
            DefaultSetting::new(Some(TestConfigData { value: 3 }), "nuthatch", None, true);

        assert_eq!(setting.get_default_value().unwrap().value, 3);
    }

    #[test]
    fn test_load_default_none() {
        let mut setting = DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch", None, true);

        assert!(setting.get_default_value().is_none());
    }
}
