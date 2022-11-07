// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::testgen::LogStatsTest;
use anyhow::format_err;
use anyhow::Error;
use diagnostics_reader::{assert_data_tree, ArchiveReader, DiagnosticsHierarchy, Inspect};
use fidl_fuchsia_component::BinderMarker;

mod testgen;

#[fuchsia::test]
async fn test_log_stats_component_tree() -> Result<(), Error> {
    let instance = LogStatsTest::create_realm().await.expect("setting up test realm");
    let log_stats_proxy = &instance.root.connect_to_named_protocol_at_exposed_dir::<BinderMarker>(
        "fuchsia.component.LogStatsBinder",
    );

    assert!(log_stats_proxy.is_ok());

    let moniker = format!("realm_builder\\:{}/log-stats", &instance.root.child_name());
    let hierarchy = get_inspect_hierarchy(&moniker).await?;

    assert_data_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "OK",
        }
    });

    Ok(())
}

async fn get_inspect_hierarchy(moniker: &str) -> Result<DiagnosticsHierarchy, Error> {
    ArchiveReader::new()
        .add_selector(format!("{}:root", moniker))
        .snapshot::<Inspect>()
        .await?
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .ok_or(format_err!("expected one inspect hierarchy"))
}
