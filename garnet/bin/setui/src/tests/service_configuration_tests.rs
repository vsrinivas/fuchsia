// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::base::SettingType,
    crate::config::default_settings::DefaultSetting,
    crate::handler::device_storage::testing::*,
    crate::policy::base::PolicyType,
    crate::AgentConfiguration,
    crate::EnabledPoliciesConfiguration,
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

pub fn get_test_policy_types() -> HashSet<PolicyType> {
    return vec![PolicyType::Unknown].into_iter().collect();
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_no_configuration_provided() {
    let factory = InMemoryStorageFactory::create();

    let default_configuration =
        EnabledServicesConfiguration::with_services(get_test_settings_types());

    let default_policy_configuration =
        EnabledPoliciesConfiguration::with_policies(get_test_policy_types());

    let flags = ServiceFlags::default();
    let configuration = ServiceConfiguration::from(
        AgentConfiguration::default(),
        default_configuration,
        default_policy_configuration,
        flags,
    );

    let env = EnvironmentBuilder::new(factory)
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    // No ServiceConfiguration provided, we should be able to connect to the service and make a watch call without issue.
    let service = env.connect_to_service::<AccessibilityMarker>().expect("Connected to service");
    service.watch().await.expect("watch completed");

    // TODO(fxbug.dev/60925): verify that the volume policy service can't be connected to once the
    // configuration is used.
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_default_configuration_provided() {
    let factory = InMemoryStorageFactory::create();

    // Load test configuration, which only has Accessibility, default will not be used.
    let configuration = DefaultSetting::new(None, "/config/data/service_configuration.json")
        .get_default_value()
        .expect("no enabled service configuration provided");

    // Load test configuration for policy, which includes Audio, default will not be used.
    let policy_configuration = DefaultSetting::new(None, "/config/data/policy_configuration.json")
        .get_default_value()
        .expect("no enabled policy configuration provided");

    let flags = ServiceFlags::default();
    let configuration = ServiceConfiguration::from(
        AgentConfiguration::default(),
        configuration,
        policy_configuration,
        flags,
    );

    let env = EnvironmentBuilder::new(factory)
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env.connect_to_service::<AccessibilityMarker>().expect("Connected to service");

    // Any calls to the privacy service should fail since the service isn't included in the configuration.
    let privacy_service = env.connect_to_service::<PrivacyMarker>().unwrap();
    privacy_service.watch().await.expect_err("watch completed");

    // TODO(fxbug.dev/60925): verify that the volume policy service can be connected to once the
    // configuration is used.
}
