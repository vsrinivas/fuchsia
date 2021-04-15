// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_hierarchy::assert_data_tree as assert_inspect_tree,
    diagnostics_reader::{ArchiveReader, Inspect},
};

#[fuchsia::test]
async fn verify_proxy_reuse() -> Result<(), Error> {
    let archive_reader = ArchiveReader::new().add_selector(
        "archivist:root/all_archive_accessor:archive_accessor_connections_opened".to_string(),
    );
    let results = archive_reader.snapshot::<Inspect>().await?;

    assert_eq!(results.len(), 1);

    assert_inspect_tree!(results[0].payload.as_ref().unwrap(), root: {
        "all_archive_accessor": {
            archive_accessor_connections_opened: 1u64,
        }
    });

    let results = archive_reader.snapshot::<Inspect>().await?;

    assert_eq!(results.len(), 1);

    assert_inspect_tree!(results[0].payload.as_ref().unwrap(), root: {
        "all_archive_accessor": {
            archive_accessor_connections_opened: 1u64,
        }
    });

    let archive_reader = ArchiveReader::new().add_selector(
        "archivist:root/all_archive_accessor:archive_accessor_connections_opened".to_string(),
    );

    let results = archive_reader.snapshot::<Inspect>().await?;

    assert_eq!(results.len(), 1);

    assert_inspect_tree!(results[0].payload.as_ref().unwrap(), root: {
        "all_archive_accessor": {
            archive_accessor_connections_opened: 2u64,
        }
    });

    Ok(())
}
