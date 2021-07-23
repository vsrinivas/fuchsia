// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ProtocolMarker,
    fidl_fidl_test_components as ftest, fidl_fuchsia_device_manager as fdevicemanager,
    fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    futures::{StreamExt, TryStreamExt},
    log::*,
};

#[fuchsia::component]
async fn main() {
    info!("start");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_system_state_transition(stream).await;
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}

async fn run_system_state_transition(
    mut stream: fdevicemanager::SystemStateTransitionRequestStream,
) {
    async move {
        match stream.try_next().await? {
            Some(fdevicemanager::SystemStateTransitionRequest::SetTerminationSystemState {
                state: _,
                responder,
            }) => {
                info!("SetTerminationState called");
                // Notify the integration test that shutdown was triggered.
                let trigger = client::connect_to_protocol::<ftest::TriggerMarker>().unwrap();
                trigger.run().await.unwrap();
                responder.send(&mut Ok(()))?;
            }
            _ => (),
        }
        Ok(())
    }
    .await
    .unwrap_or_else(|e: anyhow::Error| {
        panic!("couldn't run {}: {:?}", fdevicemanager::SystemStateTransitionMarker::NAME, e);
    });
}
