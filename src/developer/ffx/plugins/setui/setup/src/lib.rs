// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_setup_args::Setup;
use fidl_fuchsia_settings::{ConfigurationInterfaces, SetupProxy, SetupSettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", SetupProxy = "core/setui_service:expose:fuchsia.settings.Setup")]
pub async fn run_command(setup_proxy: SetupProxy, setup: Setup) -> Result<()> {
    handle_mixed_result("Setup", command(setup_proxy, setup.configuration_interfaces).await).await
}

async fn command(
    proxy: SetupProxy,
    configuration_interfaces: Option<ConfigurationInterfaces>,
) -> WatchOrSetResult {
    let mut settings = SetupSettings::EMPTY;
    settings.enabled_configuration_interfaces = configuration_interfaces;

    if settings == SetupSettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        // Default to reboot the device in order for the change to take effect.
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone(), true).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set configuration interfaces to {:?}", configuration_interfaces)
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{SetupRequest, SetupSettings};
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        const INTERFACE: ConfigurationInterfaces = ConfigurationInterfaces::ETHERNET;

        let proxy = setup_fake_setup_proxy(move |req| match req {
            SetupRequest::Set { settings, responder, .. } => {
                if let Some(val) = settings.enabled_configuration_interfaces {
                    assert_eq!(val, INTERFACE);
                    let _ = responder.send(&mut Ok(()));
                } else {
                    panic!("Unexpected call to set");
                }
            }
            SetupRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let setup = Setup { configuration_interfaces: Some(INTERFACE) };
        let response = run_command(proxy, setup).await;
        assert!(response.is_ok());
    }

    #[test_case(
        ConfigurationInterfaces::ETHERNET;
        "Test setup set() output with ethernet config."
    )]
    #[test_case(
        ConfigurationInterfaces::WIFI;
        "Test setup set() output with wifi config."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_setup_output(expected_interface: ConfigurationInterfaces) -> Result<()> {
        let proxy = setup_fake_setup_proxy(move |req| match req {
            SetupRequest::Set { settings, responder, .. } => {
                if let Some(val) = settings.enabled_configuration_interfaces {
                    assert_eq!(val, expected_interface);
                    let _ = responder.send(&mut Ok(()));
                } else {
                    panic!("Unexpected call to set");
                }
            }
            SetupRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, Some(expected_interface)));
        assert_eq!(
            output,
            format!("Successfully set configuration interfaces to {:?}", Some(expected_interface))
        );
        Ok(())
    }

    #[test_case(
        None;
        "Test setup watch() output with empty config."
    )]
    #[test_case(
        Some(ConfigurationInterfaces::ETHERNET);
        "Test setup watch() output with non-empty config."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_setup_watch_output(
        expected_interface: Option<ConfigurationInterfaces>,
    ) -> Result<()> {
        let proxy = setup_fake_setup_proxy(move |req| match req {
            SetupRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            SetupRequest::Watch { responder } => {
                let _ = responder.send(SetupSettings {
                    enabled_configuration_interfaces: expected_interface,
                    ..SetupSettings::EMPTY
                });
            }
        });

        let output = utils::assert_watch!(command(proxy, None));
        assert_eq!(
            output,
            format!(
                "{:#?}",
                SetupSettings {
                    enabled_configuration_interfaces: expected_interface,
                    ..SetupSettings::EMPTY
                }
            )
        );
        Ok(())
    }
}
