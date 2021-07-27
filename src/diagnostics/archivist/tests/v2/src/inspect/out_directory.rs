// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_topology;
use anyhow::Error;
use fidl_fuchsia_diagnostics::ArchiveAccessorMarker;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;
use std::path::Path;

#[fuchsia::test]
async fn out_can_be_read() {
    let builder = test_topology::create(test_topology::Options::default())
        .await
        .expect("create base topology");
    let instance = builder.build().create().await.expect("create instance");
    // Start the archivist by connecting to the accessor.
    let _accessor =
        instance.root.connect_to_protocol_at_exposed_dir::<ArchiveAccessorMarker>().unwrap();

    let hub_out_path_str = format!(
        "/hub/children/fuchsia_component_test_collection:{}/children/test/children/archivist/exec/out/",
        instance.root.child_name()
    );
    let hub_out_path = Path::new(&hub_out_path_str);
    verify_out(&hub_out_path).await.expect("verify - first");

    // Verify again to ensure we can continue to read.
    verify_out(&hub_out_path).await.expect("verify - second");
}

async fn verify_out(hub_out_path: &Path) -> Result<(), Error> {
    // TODO(fxbug.dev/61350): We need to retry since the out dir might not be serving yet. A much
    // cleaner way to do this would be to snapshot_then_subscribe for lifecycle events in the
    // archivist but unfortunately snapshot is the only supported mode at the moment.
    loop {
        if let Ok(mut result) = read_entries(hub_out_path).await {
            let mut expected = vec!["diagnostics".to_string(), "svc".to_string()];
            result.sort();
            expected.sort();
            if result == expected {
                break;
            }
        };
        fasync::Timer::new(fasync::Time::after(100.millis())).await;
    }

    let result = read_entries(&hub_out_path.join("diagnostics")).await.unwrap();
    let expected = vec!["fuchsia.inspect.Tree".to_string()];
    assert_eq!(expected, result,);

    Ok(())
}

async fn read_entries(path: &Path) -> Result<Vec<String>, Error> {
    let directory =
        io_util::open_directory_in_namespace(path.to_str().unwrap(), fio::OPEN_RIGHT_READABLE)?;
    Ok(files_async::readdir(&directory).await?.into_iter().map(|e| e.name).collect::<Vec<_>>())
}
