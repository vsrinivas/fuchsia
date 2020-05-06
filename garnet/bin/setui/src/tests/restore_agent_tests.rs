// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent::RestoreAgent;
use crate::registry::device_storage::testing::InMemoryStorageFactory;
use crate::switchboard::base::{
    SettingRequest, SettingResponseResult, SettingType, SwitchboardError,
};
use crate::tests::fakes::base::create_setting_handler;
use crate::EnvironmentBuilder;
use futures::lock::Mutex;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_audio_test_environment";

// Helper function for bringing up an environment with a single handler for a
// single SettingType and validating the environment initialization result.
async fn verify_restore_handling(
    response_generate: Box<dyn Fn() -> SettingResponseResult + Send + Sync + 'static>,
    success: bool,
) {
    let counter: Arc<Mutex<u64>> = Arc::new(Mutex::new(0));

    let counter_clone = counter.clone();
    assert_eq!(
        success,
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(
                SettingType::Unknown,
                create_setting_handler(Box::new(move |request| {
                    let counter = counter_clone.clone();
                    if request == SettingRequest::Restore {
                        let result = (response_generate)();
                        return Box::pin(async move {
                            let mut counter_lock = counter.lock().await;
                            *counter_lock += 1;
                            return result;
                        });
                    } else {
                        return Box::pin(async { Ok(None) });
                    }
                })),
            )
            .agents(&[Arc::new(Mutex::new(RestoreAgent::new()))])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await
            .is_ok()
    );

    assert_eq!(*counter.lock().await, 1);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_restore() {
    // Should succeed when the restore command is handled.
    verify_restore_handling(Box::new(|| Ok(None)), true).await;

    // Snould succeed when the restore command is explicitly not handled.
    verify_restore_handling(
        Box::new(|| {
            Err(SwitchboardError::UnimplementedRequest {
                setting_type: SettingType::Unknown,
                request: SettingRequest::Restore,
            })
        }),
        true,
    )
    .await;

    // Snould fail when any other error is introduced.
    verify_restore_handling(
        Box::new(|| Err(SwitchboardError::UnexpectedError { description: "foo".to_string() })),
        false,
    )
    .await;
}
