// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_inspect::TreeMarker,
    fuchsia_inspect::reader::{self, NodeHierarchy},
};

/// Get the Inspect `NodeHierarchy` for the component under test running in the nested environment.
/// Requires sandbox:features:hub capability.
pub async fn get_inspect_hierarchy(
    nested_environment_label: &str,
    component_name: &str,
) -> NodeHierarchy {
    let pattern = format!(
        "/hub/r/{}/*/c/{}/*/out/diagnostics/{}",
        glob::Pattern::escape(nested_environment_label),
        component_name,
        TreeMarker::SERVICE_NAME,
    );
    let paths = glob::glob_with(
        &pattern,
        glob::MatchOptions {
            case_sensitive: true,
            require_literal_separator: true,
            require_literal_leading_dot: false,
        },
    )
    .expect("glob pattern successfully compiles");
    let mut paths = paths.collect::<Result<Vec<_>, _>>().unwrap();
    assert_eq!(paths.len(), 1, "glob pattern: {:?}, matched paths: {:?}", pattern, paths);
    let path = paths.pop().unwrap();

    let (tree, server_end) =
        fidl::endpoints::create_proxy::<TreeMarker>().expect("failed to create Tree proxy");
    fdio::service_connect(&path.to_string_lossy().to_string(), server_end.into_channel())
        .expect("failed to connect to Tree service");
    reader::read_from_tree(&tree).await.expect("failed to get inspect hierarchy")
}
