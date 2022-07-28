// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    fidl_fuchsia_input_injection::InputDeviceRegistryMarker,
    fidl_fuchsia_ui_test_input::RegistryRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::{client::new_protocol_connector, server::ServiceFs},
    futures::StreamExt,
    tracing::info,
};

/// Note to contributors: This component is test-only, so it should panic liberally. Loud crashes
/// are much easier to debug than silent failures. Please use `expect()` and `panic!` where
/// applicable.
#[fuchsia::main(logging_tags = ["input_helper"])]
async fn main() -> Result<(), Error> {
    info!("starting input synthesis test component");

    let mut fs = ServiceFs::new_local();

    fs.dir("svc").add_fidl_service(|stream: RegistryRequestStream| {
        fasync::Task::local(async move {
            let registry_connection = new_protocol_connector::<InputDeviceRegistryMarker>()
                .expect("failed to connect to fuchsia.ui.test.input.Registry");
            if registry_connection.exists().await.expect("failed to connect to fuchsia.ui.test.input.Registry")
            {
                input_testing::handle_registry_request_stream(
                    stream,
                    registry_connection
                        .connect()
                        .expect("failed to connect to input device registry protocol"),
                )
                .await;
                info!("client closed fuchsia.ui.test.input.Registry connection");
            } else {
                panic!("failed to connect to fuchsia.ui.test.input.Registry: protocol node does not exist in /svc");
            }
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;

    Ok(())
}
