// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent;
use crate::base::SettingType;
use crate::factory_reset::types::FactoryResetInfo;
use crate::handler::base::Request;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::setting_handler::ControllerError;
use crate::ingress::fidl::Interface;
use crate::tests::fakes::base::create_setting_handler;
use crate::tests::fakes::recovery_policy_service::RecoveryPolicy;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::EnvironmentBuilder;
use fidl_fuchsia_settings::{FactoryResetMarker, FactoryResetProxy, FactoryResetSettings};
use futures::lock::Mutex;
use matches::assert_matches;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_factory_test_environment";
const STARTING_RESET: bool = true;
const CHANGED_RESET: bool = false;

async fn setup_env() -> (FactoryResetProxy, RecoveryPolicy) {
    let service_registry = ServiceRegistry::create();
    let recovery_policy_service_handler = RecoveryPolicy::create();
    service_registry
        .lock()
        .await
        .register_service(Arc::new(Mutex::new(recovery_policy_service_handler.clone())));
    let env = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .fidl_interfaces(&[Interface::FactoryReset])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let factory_reset_proxy =
        env.connect_to_protocol::<FactoryResetMarker>().expect("FactoryReset should be available");
    (factory_reset_proxy, recovery_policy_service_handler)
}

// Tests that the FIDL calls for the reset setting result in appropriate
// commands sent to the service.
#[fuchsia_async::run_until_stalled(test)]
async fn test_set() {
    let (factory_reset_proxy, recovery_policy_service_handler) = setup_env().await;

    // Validate the default value when the service starts.
    let settings = factory_reset_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.is_local_reset_allowed, Some(STARTING_RESET));

    // Validate no value has been sent to the recovery policy service.
    {
        let local_reset_allowed = recovery_policy_service_handler.is_local_reset_allowed();
        let local_reset_allowed = local_reset_allowed.lock().await;
        assert_eq!(*local_reset_allowed, None);
    }

    // Update the value.
    let mut factory_reset_settings = FactoryResetSettings::EMPTY;
    factory_reset_settings.is_local_reset_allowed = Some(CHANGED_RESET);
    factory_reset_proxy
        .set(factory_reset_settings)
        .await
        .expect("set completed")
        .expect("set successful");

    // Validate the value was sent to the recovery policy service.
    {
        let mutex = recovery_policy_service_handler.is_local_reset_allowed();
        let local_reset_allowed = mutex.lock().await;
        assert_eq!(*local_reset_allowed, Some(CHANGED_RESET));
    }

    // Validate the value is available on the next watch.
    let settings = factory_reset_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.is_local_reset_allowed, Some(CHANGED_RESET));
}

// Makes sure that settings are restored from storage when service comes online.
#[fuchsia_async::run_until_stalled(test)]
async fn test_restore() {
    // Ensure is_local_reset_allowed value is restored correctly.
    validate_restore(true).await;
    validate_restore(false).await;
}

async fn validate_restore(is_local_reset_allowed: bool) {
    let service_registry = ServiceRegistry::create();
    let recovery_policy_service_handler = RecoveryPolicy::create();
    service_registry
        .lock()
        .await
        .register_service(Arc::new(Mutex::new(recovery_policy_service_handler.clone())));

    // Set stored value.
    let storage_factory =
        InMemoryStorageFactory::with_initial_data(&FactoryResetInfo { is_local_reset_allowed });

    // Bring up environment with restore agent and factory reset.
    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::FactoryReset])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .expect("env should be available");

    // Connect to the proxy so we ensure the restore has occurred.
    let factory_reset_proxy = env
        .connect_to_protocol::<FactoryResetMarker>()
        .expect("factory reset service should be available");

    // Validate that the recovery policy service received the restored value.
    {
        let mutex = recovery_policy_service_handler.is_local_reset_allowed();
        let local_reset_allowed = mutex.lock().await;
        assert_eq!(*local_reset_allowed, Some(is_local_reset_allowed));
    }

    // Validate that the restored value is available on the next watch.
    let settings = factory_reset_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.is_local_reset_allowed, Some(is_local_reset_allowed));
}

// Tests that the FIDL calls for the reset setting result in appropriate
// commands sent to the service.
#[fuchsia_async::run_until_stalled(test)]
async fn test_error_propagation() {
    let service_registry = ServiceRegistry::create();
    let recovery_policy_service_handler = RecoveryPolicy::create();
    service_registry
        .lock()
        .await
        .register_service(Arc::new(Mutex::new(recovery_policy_service_handler.clone())));

    // Bring up environment with restore agent and factory reset.
    let env = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .handler(
            SettingType::FactoryReset,
            create_setting_handler(Box::new(move |request| {
                if request == Request::Get {
                    Box::pin(async {
                        Err(ControllerError::UnhandledType(SettingType::FactoryReset))
                    })
                } else {
                    Box::pin(async { Ok(None) })
                }
            })),
        )
        .fidl_interfaces(&[Interface::FactoryReset])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .expect("env should be available");

    // Connect to the proxy.
    let factory_reset_proxy = env
        .connect_to_protocol::<FactoryResetMarker>()
        .expect("factory reset service should be available");

    // Validate that an unavailable error is returned.
    assert_matches!(
        factory_reset_proxy.watch().await,
        Err(fidl::Error::ClientChannelClosed {
            status: fuchsia_zircon::Status::UNAVAILABLE,
            service_name: "fuchsia.settings.FactoryReset"
        })
    );
}
