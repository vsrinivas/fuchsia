// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    fidl_fuchsia_composite_test as ft, fidl_fuchsia_driver_test as fdt, fuchsia_async as fasync,
    fuchsia_async::futures::{StreamExt, TryStreamExt},
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon as zx,
    futures::channel::mpsc,
};

const WAITER_NAME: &'static str = "waiter";

async fn waiter_serve(mut stream: ft::WaiterRequestStream, mut sender: mpsc::Sender<()>) {
    while let Some(ft::WaiterRequest::Ack { status, .. }) =
        stream.try_next().await.expect("Stream failed")
    {
        assert_eq!(status, zx::Status::OK.into_raw());
        sender.try_send(()).expect("Sender failed")
    }
}

async fn waiter_component(
    handles: LocalComponentHandles,
    sender: mpsc::Sender<()>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream: ft::WaiterRequestStream| {
        fasync::Task::spawn(waiter_serve(stream, sender.clone())).detach()
    });
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    Ok(fs.collect::<()>().await)
}

#[fasync::run_singlethreaded(test)]
async fn test_composite() -> Result<()> {
    let (sender, mut receiver) = mpsc::channel(1);

    // Create the RealmBuilder.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_manifest_setup("#meta/realm.cm").await?;
    let waiter = builder
        .add_local_child(
            WAITER_NAME,
            move |handles: LocalComponentHandles| {
                Box::pin(waiter_component(handles, sender.clone()))
            },
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<ft::WaiterMarker>())
                .from(&waiter)
                .to(Ref::child(fuchsia_driver_test::COMPONENT_NAME)),
        )
        .await?;
    // Build the Realm.
    let instance = builder.build().await?;
    // Start the DriverTestRealm.
    let args = fdt::RealmArgs {
        root_driver: Some("#meta/root.cm".to_string()),
        use_driver_framework_v2: Some(true),
        ..fdt::RealmArgs::EMPTY
    };
    instance.driver_test_realm_start(args).await?;

    // Wait for the driver to call Waiter.Done.
    receiver.next().await.ok_or(anyhow!("Receiver failed"))
}
