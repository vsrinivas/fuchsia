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
use futures::channel::mpsc::Sender;
use futures::{SinkExt, StreamExt, TryStreamExt};
mod common;

#[async_trait]
impl Mocks for FactoryResetTest {
    async fn recovery_policy_impl(
        handles: LocalComponentHandles,
        reset_allowed_sender: Sender<bool>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(move |mut stream: DeviceRequestStream| {
                let mut reset_allowed_sender = reset_allowed_sender.clone();
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
                                reset_allowed_sender
                                    .send(allowed)
                                    .await
                                    .expect("Sent allowed value from SetIsLocalResetAllowed call");
                            }
                            _ => {}
                        }
                    }
                })
                .detach();
            });
        let _ = fs.serve_connection(handles.outgoing_dir)?;
        fs.collect::<()>().await;
        Ok(())
    }
}

// Tests that the FIDL calls for the reset setting result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_set() {
    // This bounded channel is initialized with 0 initial capacity so that the sender (which adds 1
    // capacity) can only send one message at a time, so that we can more closely verify when the
    // mock receives requests. This makes it so that if the mock receives two requests in a row
    // without the test verifying at least the first one, the test will fail in order to indicate
    // something unexpected has happened.
    let (reset_allowed_sender, mut reset_allowed_receiver) =
        futures::channel::mpsc::channel::<bool>(0);
    let instance =
        FactoryResetTest::create_realm(reset_allowed_sender).await.expect("setting up test realm");

    let proxy = FactoryResetTest::connect_to_factoryresetmarker(&instance);

    // Verify that the value is true by default.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.is_local_reset_allowed, Some(true));

    // Verify that the mock receives a value of true for the setting on service start.
    assert_eq!(Some(true), reset_allowed_receiver.next().await);

    // Update the value to false.
    proxy
        .set(fidl_fuchsia_settings::FactoryResetSettings {
            is_local_reset_allowed: Some(false),
            ..fidl_fuchsia_settings::FactoryResetSettings::EMPTY
        })
        .await
        .expect("set completed")
        .expect("set successful");

    // Verify that the mock receives a value of false for the setting after the set call.
    assert_eq!(Some(false), reset_allowed_receiver.next().await);
}
