// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{FactoryResetTest, Mocks};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_recovery_policy::{DeviceRequest, DeviceRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::lock::Mutex;
use futures::{StreamExt, TryStreamExt};
use std::sync::Arc;
mod common;

#[async_trait]
impl Mocks for FactoryResetTest {
    async fn recovery_policy_impl(
        handles: LocalComponentHandles,
        is_local_reset_allowed: Arc<Mutex<Option<bool>>>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(move |mut stream: DeviceRequestStream| {
                let local_reset_allowed_handle = is_local_reset_allowed.clone();
                fasync::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        // Support future expansion of FIDL.
                        #[allow(unreachable_patterns)]
                        #[allow(clippy::single_match)]
                        match req {
                            DeviceRequest::SetIsLocalResetAllowed {
                                allowed,
                                control_handle: _,
                            } => {
                                *local_reset_allowed_handle.lock().await = Some(allowed);
                            }
                            _ => {}
                        }
                    }
                })
                .detach();
            });
        let _: &mut ServiceFs<_> =
            fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
        fs.collect::<()>().await;
        Ok(())
    }
}

const STARTING_RESET: bool = true;
const CHANGED_RESET: bool = false;

// Tests that the FIDL calls for the reset setting result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_set() {
    let is_allowed = Arc::new(Mutex::new(None));
    let instance = FactoryResetTest::create_realm(Arc::clone(&is_allowed))
        .await
        .expect("setting up test realm");

    let proxy = FactoryResetTest::connect_to_factoryresetmarker(&instance);

    {
        // Validate no value has been sent to the recovery policy service after service starts.
        let local_reset_allowed = is_allowed.lock().await;
        assert_eq!(*local_reset_allowed, None);
    }

    // Validate the default value when the service starts.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.is_local_reset_allowed, Some(STARTING_RESET));

    // Update the value.
    let mut factory_reset_settings = fidl_fuchsia_settings::FactoryResetSettings::EMPTY;
    factory_reset_settings.is_local_reset_allowed = Some(CHANGED_RESET);
    proxy.set(factory_reset_settings).await.expect("set completed").expect("set successful");

    // Validate the value was sent to the recovery policy service.
    {
        let local_reset_allowed = is_allowed.lock().await;
        assert_eq!(*local_reset_allowed, Some(CHANGED_RESET));
    }

    // Ensure retrieved value matches set value
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.is_local_reset_allowed, Some(CHANGED_RESET));
}
