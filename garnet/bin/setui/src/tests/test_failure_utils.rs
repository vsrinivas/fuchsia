// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::device_storage::testing::InMemoryStorageFactory;
use crate::registry::setting_handler::ControllerError;
use crate::switchboard::base::{SettingRequest, SettingType};
use crate::tests::fakes::base::create_setting_handler;
use crate::EnvironmentBuilder;
use futures::lock::Mutex;
use std::sync::Arc;

/// Creates an environment that will fail on a get request.
///
/// Returns a NestedEnvironment so that each test can connect to its own
/// service without having to constrain the Proxy type here.
pub async fn create_test_env_with_failures(
    storage_factory: Arc<Mutex<InMemoryStorageFactory>>,
    env_name: &'static str,
    setting_type: SettingType,
) -> fuchsia_component::server::NestedEnvironment {
    EnvironmentBuilder::new(storage_factory)
        .handler(
            setting_type,
            create_setting_handler(Box::new(move |request| {
                if request == SettingRequest::Get {
                    Box::pin(async move { Err(ControllerError::UnhandledType(setting_type)) })
                } else {
                    Box::pin(async { Ok(None) })
                }
            })),
        )
        .settings(&[setting_type])
        .spawn_and_get_nested_environment(env_name)
        .await
        .unwrap()
}
