// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fuchsia_async as fasync,
    fuchsia_inspect::{assert_inspect_tree, reader::NodeHierarchy},
    std::convert::TryFrom,
};

#[fasync::run_singlethreaded(test)]
async fn test_initial_inspect_state() {
    let env = TestEnv::new();
    // Wait for inspect VMO to be created
    env.proxies
        .rewrite_engine
        .test_apply("fuchsia-pkg://test")
        .await
        .expect("fidl call succeeds")
        .expect("test apply result is ok");

    // Use glob to find the path of the inspect VMO
    // Note: the glob match only works if there is a pattern in the final path component.
    // We are not sure why this is the case, but suspect it has something to do with
    // how the vfs is implemented.
    let pattern = format!(
        "/hub/r/{}/*/c/pkg_resolver.cmx/*/out/objects/root[.]inspect",
        glob::Pattern::escape(&env.nested_environment_label)
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
    assert_eq!(paths.len(), 1, "{:?}", paths);
    let path = paths.pop().unwrap();

    // Obtain VMO and convert into node heirarchy
    let vmo_file = File::open(path).expect("file exists");
    let vmo = fdio::get_vmo_copy_from_file(&vmo_file).expect("vmo exists");
    let node_heirarchy = NodeHierarchy::try_from(&vmo).expect("create hierarchy from vmo");

    assert_inspect_tree!(
       node_heirarchy,
        root: {
            rewrite_manager: {
                dynamic_rules: {},
                dynamic_rules_path: format!("{:?}", Some(std::path::Path::new("/data/rewrites.json"))),
                static_rules: {},
                generation: 0u64,
            },
            main: {
              channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                channel_name: format!("{:?}", Option::<String>::None),
              }
            },
            experiments: {
            }
        }
    );

    env.stop().await;
}
