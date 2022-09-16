// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use component_events::{
    matcher::*,
    sequence::{EventSequence, Ordering},
};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::*;
use futures::{future::BoxFuture, FutureExt, StreamExt};
use tracing::*;
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, file::vmo::read_only_static,
    pseudo_directory,
};

// This value must be kept consistent with the value in maintainer.rs
const EXPECTED_BACKSTOP_TIME_SEC_STR: &str = "1589910459";

fn mock_boot_handles(
    handles: LocalComponentHandles,
) -> BoxFuture<'static, Result<(), anyhow::Error>> {
    // Construct a pseudo-directory to mock the component manager's configured
    // backstop time.
    let dir = pseudo_directory! {
        "config" => pseudo_directory! {
            "build_info" => pseudo_directory! {
                // The backstop time is stored in seconds.
                "minimum_utc_stamp" => read_only_static(EXPECTED_BACKSTOP_TIME_SEC_STR),
            },
        },
    };

    let (client, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let server = server.into_channel();

    let scope = ExecutionScope::new();
    dir.open(
        scope,
        fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server),
    );
    async move {
        let mut fs = ServiceFs::new();
        fs.add_remote("boot", client);
        fs.serve_connection(handles.outgoing_dir).expect("serve mock ServiceFs");
        fs.collect::<()>().await;
        Ok(())
    }
    .boxed()
}

#[fuchsia::test(logging_minimum_severity = "warn")]
async fn builtin_time_service_and_clock_routed() {
    // Define the realm inside component manager.
    let builder = RealmBuilder::new().await.unwrap();
    let realm =
        builder.add_child("realm", "#meta/realm.cm", ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&realm),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.time.Maintenance"))
                .from(Ref::parent())
                .to(&realm),
        )
        .await
        .unwrap();

    let (component_manager_realm, _task) =
        builder.with_nested_component_manager("#meta/component_manager.cm").await.unwrap();

    // Define a mock component that serves the `/boot` directory to component manager
    let mock_boot = component_manager_realm
        .add_local_child("mock_boot", mock_boot_handles, ChildOptions::new())
        .await
        .unwrap();
    component_manager_realm
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("boot").path("/boot").rights(fio::Operations::all()),
                )
                .from(&mock_boot)
                .to(Ref::child("component_manager")),
        )
        .await
        .unwrap();

    let instance = component_manager_realm.build().await.unwrap();

    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventStream2Marker>().unwrap();

    let event_stream = component_events::events::EventStream::new_v2(proxy);

    // Unblock the component_manager.
    debug!("starting component tree");
    instance.start_component_tree().await.unwrap();

    // Wait for both components to exit cleanly.
    // The child components do several assertions on UTC time properties.
    // If any assertion fails, the component will fail with non-zero exit code.
    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .stop(Some(ExitStatusMatcher::Clean))
                    .moniker("./realm/time_client"),
                EventMatcher::ok()
                    .stop(Some(ExitStatusMatcher::Clean))
                    .moniker("./realm/maintainer"),
            ],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
