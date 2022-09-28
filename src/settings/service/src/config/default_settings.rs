// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use serde::de::DeserializeOwned;
use std::fmt::{Debug, Display};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::sync::Arc;

use crate::config;
use crate::config::base::ConfigLoadInfo;
use crate::inspect::config_logger::{InspectConfigLogger, InspectConfigLoggerHandle};

pub struct DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone + Debug,
    P: AsRef<Path> + Display,
{
    default_value: Option<T>,
    config_file_path: P,
    cached_value: Option<Option<T>>,
    config_logger: Arc<Mutex<InspectConfigLogger>>,
}

impl<T, P> DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone + std::fmt::Debug,
    P: AsRef<Path> + Display,
{
    pub fn new(default_value: Option<T>, config_file_path: P) -> Self {
        let inspect_config_logger_handle = InspectConfigLoggerHandle::new();
        DefaultSetting {
            default_value,
            config_file_path,
            cached_value: None,
            config_logger: inspect_config_logger_handle.logger,
        }
    }

    /// Returns the value of this setting. Loads the value from storage if it hasn't been loaded
    /// before, otherwise returns a cached value.
    pub fn get_cached_value(&mut self) -> Result<Option<T>, Error> {
        if self.cached_value.is_none() {
            self.cached_value = Some(self.load_default_settings()?);
        }

        Ok(self.cached_value.as_ref().expect("cached value not present").clone())
    }

    /// Loads the value of this setting from storage.
    ///
    /// If the value isn't present, returns the default value.
    pub fn load_default_value(&mut self) -> Result<Option<T>, Error> {
        Ok(self.load_default_settings()?)
    }

    /// Attempts to load the settings from the given config_file_path.
    ///
    /// Returns the default value if unable to read or parse the file. The returned option will
    /// only be None if the default_value was provided as None.
    fn load_default_settings(&mut self) -> Result<Option<T>, Error> {
        let config_load_info: Option<ConfigLoadInfo>;
        let path = self.config_file_path.to_string();
        let load_result = match File::open(self.config_file_path.as_ref()) {
            Ok(file) => {
                match serde_json::from_reader(BufReader::new(file)) {
                    Ok(config) => {
                        // Success path.
                        config_load_info = Some(ConfigLoadInfo {
                            status: config::base::ConfigLoadStatus::Success,
                            contents: if let Some(ref payload) = config {
                                Some(format!("{:?}", payload))
                            } else {
                                None
                            },
                        });
                        Ok(config)
                    }
                    Err(e) => {
                        // Found file, but failed to parse.
                        let err_msg = format!("unable to parse config: {:?}", e);
                        config_load_info = Some(ConfigLoadInfo {
                            status: config::base::ConfigLoadStatus::ParseFailure(err_msg.clone()),
                            contents: None,
                        });
                        Err(format_err!("{:?}", err_msg))
                    }
                }
            }
            Err(..) => {
                // No file found.
                config_load_info = Some(ConfigLoadInfo {
                    status: config::base::ConfigLoadStatus::UsingDefaults(
                        "File not found, using defaults".to_string(),
                    ),
                    contents: None,
                });
                Ok(self.default_value.clone())
            }
        };
        if let Some(config_load_info) = config_load_info {
            self.write_config_load_to_inspect(path, config_load_info);
        } else {
            fx_log_err!("Could not load config for {:?}", path);
        }

        load_result
    }

    /// Attempts to write the config load to inspect.
    fn write_config_load_to_inspect(
        &mut self,
        path: String,
        config_load_info: config::base::ConfigLoadInfo,
    ) {
        let config_logger = self.config_logger.clone();
        fasync::Task::spawn(async move {
            config_logger.lock().await.write_config_load_to_inspect(path, config_load_info);
        })
        .detach();
    }
}

#[cfg(test)]
pub(crate) mod testing {
    use super::*;

    use crate::clock;
    use crate::inspect::config_logger::InspectConfigLoggerHandle;
    use crate::tests::helpers::move_executor_forward_and_get;

    use assert_matches::assert_matches;
    use fuchsia_async::TestExecutor;
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_inspect::testing::AnyProperty;
    use fuchsia_zircon::Time;
    use serde::Deserialize;

    #[derive(Clone, Debug, Deserialize)]
    struct TestConfigData {
        value: u32,
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_load_valid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_config_data.json",
        );

        assert_eq!(
            setting.load_default_value().expect("Failed to get default value").unwrap().value,
            10
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_load_invalid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_invalid_config_data.json",
        );
        assert!(setting.load_default_value().is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_load_invalid_config_file_path() {
        let mut setting = DefaultSetting::new(Some(TestConfigData { value: 3 }), "nuthatch");

        assert_eq!(
            setting.load_default_value().expect("Failed to get default value").unwrap().value,
            3
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_load_default_none() {
        let mut setting = DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch");

        assert!(setting.load_default_value().expect("Failed to get default value").is_none());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_no_inspect_write() {
        let mut setting = DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch");

        assert!(setting.load_default_value().expect("Failed to get default value").is_none());
    }

    #[test]
    fn test_config_inspect_write() {
        clock::mock::set(Time::from_nanos(0));

        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

        let mut setting = DefaultSetting::new(Some(TestConfigData { value: 3 }), "nuthatch");
        let load_result = move_executor_forward_and_get(
            &mut executor,
            async { setting.load_default_value() },
            "Unable to get default value",
        );

        assert_matches!(load_result, Ok(Some(TestConfigData { value: 3 })));

        let logger_handle = InspectConfigLoggerHandle::new();
        let lock_future = logger_handle.logger.lock();
        let inspector = move_executor_forward_and_get(
            &mut executor,
            lock_future,
            "Couldn't get inspect logger lock",
        )
        .inspector;
        assert_data_tree!(inspector, root: {
            config_loads: {
                "nuthatch": {
                    "count": AnyProperty,
                    "result_counts": {
                        "UsingDefaults": 1u64,
                    },
                    "timestamp": "0.000000000",
                    "value": "ConfigLoadInfo {\n    status: UsingDefaults(\n        \"File not found, using defaults\",\n    ),\n    contents: None,\n}",
                }
            }
        });
    }
}
