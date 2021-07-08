// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::sync::Arc,
    test_manager_lib::AboveRootCapabilitiesForTest,
    tracing::{info, warn},
};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    info!("started");
    let mut fs = ServiceFs::new();
    let test_map = test_manager_lib::TestMap::new(zx::Duration::from_minutes(5));
    let test_map_clone = test_map.clone();
    let test_map_clone2 = test_map.clone();

    let routing_info = Arc::new(AboveRootCapabilitiesForTest::new("test_manager.cm").await?);
    let routing_info_clone = routing_info.clone();

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            let test_map = test_map_clone2.clone();
            let routing_info_for_task = routing_info_clone.clone();
            fasync::Task::spawn(async move {
                test_manager_lib::run_test_manager(stream, test_map.clone(), routing_info_for_task)
                    .await
                    .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        })
        .add_fidl_service(move |stream| {
            let test_map = test_map_clone.clone();
            let routing_info_for_task = routing_info.clone();
            fasync::Task::spawn(async move {
                test_manager_lib::run_test_manager_query_server(
                    stream,
                    test_map.clone(),
                    routing_info_for_task,
                )
                .await
                .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        })
        .add_fidl_service(move |stream| {
            let test_map = test_map.clone();
            fasync::Task::spawn(async move {
                test_manager_lib::run_test_manager_info_server(stream, test_map.clone())
                    .await
                    .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
