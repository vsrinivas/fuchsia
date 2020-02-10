// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent::RestoreAgent;
use crate::registry::base::Command;
use crate::registry::device_storage::testing::InMemoryStorageFactory;
use crate::switchboard::base::{
  SettingRequest, SettingResponseResult, SettingType, SwitchboardError,
};
use crate::tests::fakes::base::create_setting_handler;
use crate::EnvironmentBuilder;
use crate::Runtime;
use anyhow::{format_err, Error};
use futures::executor::block_on;
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
  if let Ok(environment) =
    EnvironmentBuilder::new(Runtime::Nested(ENV_NAME), InMemoryStorageFactory::create_handle())
      .handler(
        SettingType::Unknown,
        create_setting_handler(Box::new(move |command| {
          if let Command::HandleRequest(SettingRequest::Restore, responder) = command {
            let mut counter_lock = block_on(counter_clone.lock());
            *counter_lock += 1;
            responder.send((response_generate)()).ok();
          }
        })),
      )
      .settings(&[SettingType::Unknown])
      .agents(&[Arc::new(Mutex::new(RestoreAgent::new()))])
      .settings(&[SettingType::Unknown])
      .spawn()
  {
    if let Ok(result) = environment.completion_rx.await {
      assert!(result.is_ok(), success);
      assert_eq!(*counter.lock().await, 1);
    } else {
      panic!("Completion rx should have returned the environment initialization result");
    }
  } else {
    panic!("Should have successfully created environment");
  }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_restore() {
  // Should succeed when the restore command is handled.
  verify_restore_handling(Box::new(|| Ok(None)), true).await;

  // Snould succeed when the restore command is explicitly not handled.
  verify_restore_handling(
    Box::new(|| {
      Err(Error::new(SwitchboardError::UnimplementedRequest {
        setting_type: SettingType::Unknown,
        request: SettingRequest::Restore,
      }))
    }),
    true,
  )
  .await;

  // Snould fail when any other error is introduced.
  verify_restore_handling(Box::new(|| Err(format_err!("unexpected error"))), false).await;
}
