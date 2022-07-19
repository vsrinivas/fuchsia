// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::storage::storage_factory::testing::InMemoryStorageFactory;
use crate::base::SettingType;
use crate::ingress::fidl::Interface;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use assert_matches::assert_matches;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_settings::*;
use fuchsia_zircon::Status;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_accessibility_test_environment";

// Creates an environment that will fail on a get request.
async fn create_a11y_test_env_with_failures(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> AccessibilityProxy {
    create_test_env_with_failures(
        storage_factory,
        ENV_NAME,
        Interface::Accessibility,
        SettingType::Accessibility,
    )
    .await
    .connect_to_protocol::<AccessibilityMarker>()
    .unwrap()
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let accessibility_proxy =
        create_a11y_test_env_with_failures(Arc::new(InMemoryStorageFactory::new())).await;
    let result = accessibility_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
