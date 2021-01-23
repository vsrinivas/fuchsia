// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_reader::{ArchiveReader, Inspect},
    fuchsia_async as fasync,
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
};

#[fasync::run_singlethreaded(test)]
async fn test_connect_to_accessor() {
    let data = ArchiveReader::new()
        .add_selector("driver/inspect-publisher:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 1);
    assert_eq!(data[0].moniker, "driver/inspect-publisher");
    assert_inspect_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
}
