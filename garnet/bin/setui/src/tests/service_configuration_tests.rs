// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::config::default_settings::DefaultSetting,
    crate::registry::device_storage::testing::*,
    crate::switchboard::base::SettingType,
    crate::EnabledServicesConfiguration,
    crate::EnvironmentBuilder,
    crate::ServiceConfiguration,
    crate::ServiceFlags,
    fidl_fuchsia_settings::{AccessibilityMarker, PrivacyMarker},
    std::collections::HashSet,
};

const ENV_NAME: &str = "settings_service_configuration_test_environment";

pub fn get_test_settings_types() -> HashSet<SettingType> {
    return vec![SettingType::Accessibility, SettingType::Privacy].into_iter().collect();
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_no_configuration_provided() {
    let factory = InMemoryStorageFactory::create();

    let default_configuration =
        EnabledServicesConfiguration::with_services(get_test_settings_types());

    // Don't load a real configuration, use the default configuration.
    let configuration = DefaultSetting::new(default_configuration, Some("not_a_real_path.json"))
        .get_default_value();
    let flags = DefaultSetting::new(ServiceFlags::default(), Some("not_a_real_path.json"))
        .get_default_value();
    let configuration = ServiceConfiguration::from(configuration, flags);

    let env = EnvironmentBuilder::new(factory)
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    // No ServiceConfiguration provided, we should be able to connect to the service and make a watch call without issue.
    let service = env.connect_to_service::<AccessibilityMarker>().expect("Connected to service");

    service.watch().await.expect("watch completed");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_default_configuration_provided() {
    let factory = InMemoryStorageFactory::create();

    let default_configuration =
        EnabledServicesConfiguration::with_services(get_test_settings_types());

    // Load test configuration, which only has Accessibility, default will not be used.
    let configuration =
        DefaultSetting::new(default_configuration, Some("/config/data/service_configuration.json"))
            .get_default_value();
    let flags = DefaultSetting::new(ServiceFlags::default(), Some("not_a_real_path.json"))
        .get_default_value();
    let configuration = ServiceConfiguration::from(configuration, flags);

    let env = EnvironmentBuilder::new(factory)
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env.connect_to_service::<AccessibilityMarker>().expect("Connected to service");

    let privacy_service = env.connect_to_service::<PrivacyMarker>().unwrap();

    // Any calls to the privacy service should fail since the service isn't included in the configuration.
    privacy_service.watch().await.expect_err("watch completed");
}
