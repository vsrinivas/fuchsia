// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::config::default_settings::DefaultSetting;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::policy::PolicyType;
use crate::tests::fakes::audio_core_service;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::AgentConfiguration;
use crate::EnabledPoliciesConfiguration;
use crate::EnabledServicesConfiguration;
use crate::EnvironmentBuilder;
use crate::ServiceConfiguration;
use crate::ServiceFlags;
use fidl_fuchsia_settings::{AccessibilityMarker, PrivacyMarker};
use fidl_fuchsia_settings_policy::VolumePolicyControllerMarker;
use std::collections::HashSet;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_configuration_test_environment";

fn get_test_settings_types() -> HashSet<SettingType> {
    return [SettingType::Accessibility, SettingType::Privacy].iter().copied().collect();
}

fn get_test_policy_types() -> HashSet<PolicyType> {
    return [PolicyType::Unknown].iter().copied().collect();
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_no_configuration_provided() {
    let factory = InMemoryStorageFactory::new();

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

    let env = EnvironmentBuilder::new(Arc::new(factory))
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    // No ServiceConfiguration provided, we should be able to connect to the service and make a watch call without issue.
    let service = env.connect_to_protocol::<AccessibilityMarker>().expect("Connected to service");
    service.watch().await.expect("watch completed");

    // No ServiceConfiguration provided, audio policy should not be able to connect.
    let policy = env
        .connect_to_protocol::<VolumePolicyControllerMarker>()
        .expect("Connected to policy service");
    policy.get_properties().await.expect_err("Policy get should fail");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_default_configuration_provided() {
    let factory = InMemoryStorageFactory::new();

    // Load test configuration, which only has Accessibility, default will not be used.
    let configuration = DefaultSetting::new(None, "/config/data/service_configuration.json")
        .load_default_value()
        .expect("invalid service configuration")
        .expect("no enabled service configuration provided");

    let flags = ServiceFlags::default();
    let configuration = ServiceConfiguration::from(
        AgentConfiguration::default(),
        configuration,
        EnabledPoliciesConfiguration::with_policies(get_test_policy_types()),
        flags,
    );

    let env = EnvironmentBuilder::new(Arc::new(factory))
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env.connect_to_protocol::<AccessibilityMarker>().expect("Connected to service");

    // Any calls to the privacy service should fail since the service isn't included in the configuration.
    let privacy_service = env.connect_to_protocol::<PrivacyMarker>().unwrap();
    privacy_service.watch().await.expect_err("watch completed");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_default_policy_configuration_provided() {
    let factory = InMemoryStorageFactory::new();

    // Load test configuration for policy which includes Audio, default will not be used.
    let policy_configuration = DefaultSetting::new(None, "/config/data/policy_configuration.json")
        .load_default_value()
        .expect("invalid policy configuration provided")
        .expect("no enabled policy configuration provided");

    let flags = ServiceFlags::default();
    let configuration = ServiceConfiguration::from(
        AgentConfiguration::default(),
        // Include audio setting so audio policy works.
        EnabledServicesConfiguration::with_services([SettingType::Audio].iter().copied().collect()),
        policy_configuration,
        flags,
    );

    // Include fake audio core service so we can be sure there's no funkiness if audio setting
    // connects to the real audio core.
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = audio_core_service::Builder::new().build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    let env = EnvironmentBuilder::new(Arc::new(factory))
        .service(ServiceRegistry::serve(service_registry))
        .configuration(configuration)
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env.connect_to_protocol::<AccessibilityMarker>().expect("Connected to service");

    // Service configuration includes volume policy and audio setting, so calls to volume policy
    // will succeed.
    let policy = env
        .connect_to_protocol::<VolumePolicyControllerMarker>()
        .expect("Connected to policy service");
    policy.get_properties().await.expect("Policy get should succeed");
}
