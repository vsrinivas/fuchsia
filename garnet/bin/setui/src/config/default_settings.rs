// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use serde::de::DeserializeOwned;
use std::fmt::Display;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

use crate::config;
use crate::event;
use crate::message::base::{role, Audience};
use crate::service::{message::Messenger, Role};

pub struct DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone,
    P: AsRef<Path> + Display,
{
    default_value: Option<T>,
    config_file_path: P,
    cached_value: Option<Option<T>>,
    messenger: Option<Messenger>,
    // Whether to log the config loads into inspect.
    log_config_loads: bool,
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
            messenger,
            log_config_loads,
        }
    }

    pub fn get_default_value(&mut self) -> Result<Option<T>, Error> {
        if self.cached_value.is_none() {
            self.cached_value = Some(self.load_default_settings()?);
        }

        Ok(self.cached_value.as_ref().expect("cached value not present").clone())
    }

    /// Attempts to load the settings from the given config_file_path.
    ///
    /// Returns the default value if unable to read or parse the file. The returned option will
    /// only be None if the default_value was provided as None.
    fn load_default_settings(&self) -> Result<Option<T>, Error> {
        if let Ok(file) = File::open(self.config_file_path.as_ref()) {
            match serde_json::from_reader(BufReader::new(file)) {
                Ok(config) => {
                    // Success path.
                    self.report_config_load(config::base::Event::Load(
                        config::base::ConfigLoadInfo {
                            path: self.config_file_path.to_string(),
                            status: config::base::ConfigLoadStatus::Success,
                        },
                    ));
                    Ok(config)
                }
                Err(e) => {
                    // Found file, but failed to parse.
                    let err_msg = format!("unable to parse config: {:?}", e);
                    self.report_config_load(config::base::Event::Load(
                        config::base::ConfigLoadInfo {
                            path: self.config_file_path.to_string(),
                            status: config::base::ConfigLoadStatus::ParseFailure(err_msg.clone()),
                        },
                    ));
                    Err(format_err!("{:?}", err_msg))
                }
            }
        } else {
            // No file found.
            self.report_config_load(config::base::Event::Load(config::base::ConfigLoadInfo {
                path: self.config_file_path.to_string(),
                status: config::base::ConfigLoadStatus::UsingDefaults(
                    "File not found, using defaults".to_string(),
                ),
            }));
            Ok(self.default_value.clone())
        }
    }

    /// If `log_config_loads` is true and a `messenger` is provided, sends a message
    /// that the config load failed. The corresponding agent will be responsible for handling
    /// the message.
    fn report_config_load(&self, config_event: config::base::Event) {
        if self.log_config_loads {
            if let Some(messenger) = self.messenger.as_ref() {
                messenger
                    .message(
                        event::Payload::Event(event::Event::ConfigLoad(config_event)).into(),
                        Audience::Role(role::Signature::role(Role::Event(event::Role::Sink))),
                    )
                    .send()
                    .ack();
            }
        }
    }
}

#[cfg(test)]
pub mod testing {
    use super::*;

    use crate::agent::Context;
    use crate::message::base::{filter, MessengerType};
    use crate::service;
    use crate::service::message::create_hub;
    use crate::tests::message_utils::verify_payload;

    use serde::Deserialize;
    use std::collections::HashSet;

    #[derive(Clone, Debug, Deserialize)]
    struct TestConfigData {
        value: u32,
    }

    async fn create_context() -> Context {
        Context::new(
            create_hub().create(MessengerType::Unbound).await.expect("should be present").1,
            create_hub(),
            HashSet::new(),
            HashSet::new(),
            None,
        )
        .await
    }

    #[test]
    fn test_load_valid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_config_data.json",
            None,
            true,
        );

        assert_eq!(
            setting.get_default_value().expect("Failed to get default value").unwrap().value,
            10
        );
    }

    #[test]
    fn test_load_invalid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_invalid_config_data.json",
            None,
            true,
        );
        assert!(setting.get_default_value().is_err());
    }

    #[test]
    fn test_load_invalid_config_file_path() {
        let mut setting =
            DefaultSetting::new(Some(TestConfigData { value: 3 }), "nuthatch", None, true);

        assert_eq!(
            setting.get_default_value().expect("Failed to get default value").unwrap().value,
            3
        );
    }

    #[test]
    fn test_load_default_none() {
        let mut setting = DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch", None, true);

        assert!(setting.get_default_value().expect("Failed to get default value").is_none());
    }

    #[test]
    fn test_no_inspect_write() {
        let mut setting =
            DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch", None, false);

        assert!(setting.get_default_value().expect("Failed to get default value").is_none());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_config_inspect_write() {
        let context = create_context().await;
        let (_, mut receptor) = context
            .delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Audience(Audience::Role(role::Signature::role(Role::Event(
                    event::Role::Sink,
                )))),
            ))))
            .await
            .expect("could not create broker");

        let (messenger, _) = context.delegate.create(MessengerType::Unbound).await.unwrap();

        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "nuthatch",
            Some(messenger),
            true,
        );
        let _ = setting.get_default_value();

        verify_payload(
            service::Payload::Event(event::Payload::Event(event::Event::ConfigLoad(
                config::base::Event::Load(config::base::ConfigLoadInfo {
                    path: "nuthatch".to_string(),
                    status: config::base::ConfigLoadStatus::UsingDefaults(
                        "File not found, using defaults".to_string(),
                    ),
                }),
            ))),
            &mut receptor,
            None,
        )
        .await;
    }
}
