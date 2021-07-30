// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    diagnostics_hierarchy::assert_data_tree,
    diagnostics_reader::{ArchiveReader, Inspect},
};

#[fuchsia::test]
async fn verify_proxy_reuse() -> Result<(), Error> {
    let mut archive_reader = ArchiveReader::new();
    archive_reader
        .add_selector("archivist:root/archive_accessor_stats/all:connections_opened".to_string());
    let results = archive_reader.snapshot::<Inspect>().await?;

    assert_eq!(results.len(), 1);

    assert_data_tree!(results[0].payload.as_ref().unwrap(), root: {
        archive_accessor_stats: {
            all: {
                connections_opened: 1u64,
            }
        }
    });

    let results = archive_reader.snapshot::<Inspect>().await?;

    assert_eq!(results.len(), 1);

    assert_data_tree!(results[0].payload.as_ref().unwrap(), root: {
        archive_accessor_stats: {
            all: {
                connections_opened: 1u64,
            }
        }
    });

    let results = ArchiveReader::new()
        .add_selector("archivist:root/archive_accessor_stats/all:connections_opened".to_string())
        .snapshot::<Inspect>()
        .await?;

    assert_eq!(results.len(), 1);

    assert_data_tree!(results[0].payload.as_ref().unwrap(), root: {
        archive_accessor_stats: {
            all: {
                connections_opened: 2u64,
            }
        }
    });

    Ok(())
}
