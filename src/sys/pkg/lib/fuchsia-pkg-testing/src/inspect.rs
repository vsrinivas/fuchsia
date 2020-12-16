// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_hierarchy::DiagnosticsHierarchy,
    diagnostics_reader::{ArchiveReader, ComponentSelector},
};

/// Get the Inspect `NodeHierarchy` for the component under test running in the nested environment.
/// Requires observer.cmx injected service.
pub async fn get_inspect_hierarchy(
    nested_environment_label: &str,
    component_name: &str,
) -> DiagnosticsHierarchy {
    ArchiveReader::new()
        .add_selector(ComponentSelector::new(vec![
            nested_environment_label.to_string(),
            component_name.to_string(),
        ]))
        .get()
        .await
        .expect("read inspect hierarchy")
        .into_iter()
        .next()
        .expect("there's one result")
        .payload
        .expect("payload is not none")
}
