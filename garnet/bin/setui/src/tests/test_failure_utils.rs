// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::storage::device_storage::testing::InMemoryStorageFactory;
use crate::base::SettingType;
use crate::handler::base::Request;
use crate::handler::setting_handler::ControllerError;
use crate::ingress::fidl::Interface;
use crate::tests::fakes::base::create_setting_handler;
use crate::EnvironmentBuilder;
use std::sync::Arc;

pub(crate) async fn create_test_env_with_failures(
    storage_factory: Arc<InMemoryStorageFactory>,
    env_name: &'static str,
    interface: Interface,
    setting_type: SettingType,
) -> fuchsia_component::server::NestedEnvironment {
    EnvironmentBuilder::new(storage_factory)
        .fidl_interfaces(&[interface])
        .handler(
            setting_type,
            create_setting_handler(Box::new(move |request| {
                if request == Request::Get {
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
