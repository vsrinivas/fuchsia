// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_driver_test as fdt,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_test_structuredconfig_receiver as scr, fidl_test_structuredconfig_receiver_shim as scrs,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::{Capability, RealmBuilder, Ref, Route},
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    futures::StreamExt,
    std::time::Duration,
};

enum IncomingRequest {
    Puppet(scr::ConfigReceiverPuppetRequestStream),
}

async fn connect_to_puppet(
    expose_dir: &DirectoryProxy,
) -> anyhow::Result<scr::ConfigReceiverPuppetProxy> {
    // Find an instance of `ConfigService`.
    let instance_name;
    let service =
        fuchsia_component::client::open_service_at_dir::<scrs::ConfigServiceMarker>(expose_dir)?;
    loop {
        // TODO(fxbug.dev/4776): Once component manager supports watching for
        // service instances, this loop should be replaced by a watcher.
        let entries = files_async::readdir(&service).await?;
        if let Some(entry) = entries.iter().next() {
            instance_name = entry.name.clone();
            break;
        }
        fasync::Timer::new(Duration::from_millis(100)).await;
    }

    // Connect to `ConfigService`.
    let config_service = fuchsia_component::client::connect_to_service_instance_at_dir::<
        scrs::ConfigServiceMarker,
    >(expose_dir, &instance_name)?;

    // TODO(fxbug.dev/94727): The test tries to connect to the shim's exposed Puppet capability.
    // We should be able to pass the server end from the test directly to the driver.
    let puppet = config_service.puppet()?;
    Ok(puppet)
}

#[fuchsia::component]
async fn main() -> anyhow::Result<()> {
    // Create the RealmBuilder and start the driver.
    let builder = RealmBuilder::new().await?;
    builder.driver_test_realm_manifest_setup("#meta/realm.cm").await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::service::<scrs::ConfigServiceMarker>())
                .from(Ref::child(fuchsia_driver_test::COMPONENT_NAME))
                .to(Ref::parent()),
        )
        .await?;
    let realm = builder.build().await?;
    let args = fdt::RealmArgs {
        root_driver: Some("#meta/cpp_driver_receiver.cm".to_string()),
        use_driver_framework_v2: Some(true),
        ..fdt::RealmArgs::EMPTY
    };
    realm.driver_test_realm_start(args).await?;

    let puppet = connect_to_puppet(realm.root.get_exposed_dir()).await?;
    let receiver_config = puppet.get_config().await?;

    // Serve this configuration back to the test
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingRequest::Puppet);
    fs.take_and_serve_directory_handle().unwrap();
    fs.for_each_concurrent(None, move |request: IncomingRequest| {
        let mut receiver_config = receiver_config.clone();
        async move {
            match request {
                IncomingRequest::Puppet(mut reqs) => {
                    while let Some(Ok(req)) = reqs.next().await {
                        match req {
                            scr::ConfigReceiverPuppetRequest::GetConfig { responder } => {
                                responder.send(&mut receiver_config).unwrap()
                            }
                        }
                    }
                }
            }
        }
    })
    .await;
    Ok(())
}
