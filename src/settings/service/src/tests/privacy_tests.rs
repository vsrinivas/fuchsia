// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::ingress::fidl::Interface;
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use assert_matches::assert_matches;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_settings::{PrivacyMarker, PrivacyProxy};
use fuchsia_zircon::Status;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_privacy_test_environment";

/// Creates an environment that will fail on a get request.
async fn create_privacy_test_env_with_failures() -> PrivacyProxy {
    let storage_factory = InMemoryStorageFactory::new();
    create_test_env_with_failures(
        Arc::new(storage_factory),
        ENV_NAME,
        Interface::Privacy,
        SettingType::Privacy,
    )
    .await
    .connect_to_protocol::<PrivacyMarker>()
    .unwrap()
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let privacy_service = create_privacy_test_env_with_failures().await;
    let result = privacy_service.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
