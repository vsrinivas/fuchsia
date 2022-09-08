// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::base::Request;
use crate::handler::setting_handler::ControllerError;
use crate::ingress::fidl::Interface;
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::fakes::base::create_setting_handler;
use crate::tests::fakes::recovery_policy_service::RecoveryPolicy;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::EnvironmentBuilder;
use assert_matches::assert_matches;
use fidl_fuchsia_settings::FactoryResetMarker;
use futures::lock::Mutex;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_factory_test_environment";

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
                    return Box::pin(async {
                        Err(ControllerError::UnhandledType(SettingType::FactoryReset))
                    });
                } else {
                    return Box::pin(async { Ok(None) });
                }
            })),
        )
        .fidl_interfaces(&[Interface::FactoryReset])
        .spawn_and_get_protocol_connector(ENV_NAME)
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
            protocol_name: "fuchsia.settings.FactoryReset"
        })
    );
}
