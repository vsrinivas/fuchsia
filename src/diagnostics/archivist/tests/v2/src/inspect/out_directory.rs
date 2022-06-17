// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_topology;
use anyhow::Error;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;
use std::path::Path;

#[fuchsia::test]
async fn out_can_be_read() {
    let (builder, _test_realm) = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    let instance = builder.build().await.expect("create instance");
    // Start the archivist by connecting to the accessor.
    let _accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let archivist_moniker =
        format!("./realm_builder:{}/test/archivist", instance.root.child_name());

    verify_out(&archivist_moniker).await.expect("verify - first");

    // Verify again to ensure we can continue to read.
    verify_out(&archivist_moniker).await.expect("verify - second");
}

async fn verify_out(moniker: &str) -> Result<(), Error> {
    // TODO(fxbug.dev/61350): We need to retry since the out dir might not be serving yet. A much
    // cleaner way to do this would be to snapshot_then_subscribe for lifecycle events in the
    // archivist but unfortunately snapshot is the only supported mode at the moment.
    let realm_query =
        fuchsia_component::client::connect_to_protocol::<fsys::RealmQueryMarker>().unwrap();
    let out_dir = loop {
        if let Ok((_, resolved)) = realm_query.get_instance_info(moniker).await.unwrap() {
            if let Some(resolved) = resolved {
                if let Some(started) = resolved.started {
                    let out_dir = started.out_dir.unwrap().into_proxy().unwrap();
                    if let Ok(mut result) = read_entries(&out_dir).await {
                        let mut expected = vec!["diagnostics".to_string(), "svc".to_string()];
                        result.sort();
                        expected.sort();
                        if result == expected {
                            break out_dir;
                        }
                    };
                }
            }
        }
        fasync::Timer::new(fasync::Time::after(100.millis())).await;
    };

    let diagnostics_dir = fuchsia_fs::open_directory(
        &out_dir,
        Path::new("diagnostics"),
        fio::OpenFlags::RIGHT_READABLE,
    )
    .unwrap();
    let result = read_entries(&diagnostics_dir).await.unwrap();
    let expected = vec!["fuchsia.inspect.Tree".to_string()];
    assert_eq!(expected, result,);

    Ok(())
}

async fn read_entries(out_dir: &fio::DirectoryProxy) -> Result<Vec<String>, Error> {
    Ok(fuchsia_fs::directory::readdir(out_dir)
        .await?
        .into_iter()
        .map(|e| e.name)
        .collect::<Vec<_>>())
}
