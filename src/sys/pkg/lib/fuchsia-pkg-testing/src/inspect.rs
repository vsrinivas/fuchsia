// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::reader::{NodeHierarchy, PartialNodeHierarchy},
    std::{convert::TryFrom as _, fs::File},
};

/// Get the Inspect `NodeHierarchy` for the component under test running in the nested environment.
/// Requires sandbox:features:hub capability.
pub async fn get_inspect_hierarchy(
    nested_environment_label: &str,
    component_name: &str,
) -> NodeHierarchy {
    // When `glob` is matching a path component that is a string literal, it uses
    // `std::fs::metadata()` to test the existence of the path instead of listing the parent dir.
    // `metadata()` calls `stat`, which creates and destroys an fd in fdio.
    // When the fd is for "root.inspect", which is a VMO, destroying the fd calls
    // `zxio_vmofile_release`, which makes a fuchsia.io.File.Seek FIDL call.
    // This FIDL call is received by `ServiceFs`, which, b/c "root.inspect" was opened
    // by fdio with `OPEN_FLAG_NODE_REFERENCE`, is treating the zircon channel as a stream of
    // Node requests.
    // `ServiceFs` then closes the channel and logs a
    // "ServiceFs failed to parse an incoming node request: UnknownOrdinal" error (with
    // the File.Seek ordinal).
    // `ServiceFs` closing the channel is seen by `metadata` as a `BrokenPipe` error, which
    // `glob` interprets as there being nothing at "root.inspect", so the VMO is not found.
    // To work around this, we use a trivial pattern in the "root.inspect" path component,
    // which prevents the `metadata` shortcut.
    //
    // To fix this, `zxio_vmofile_release` probably shouldn't be unconditionally calling
    // `fuchsia.io.File.Seek`, because, per a comment in `io.fidl`, that is not a valid
    // method to be called on a `Node` opened with `OPEN_FLAG_NODE_REFERENCE`.
    // `zxio_vmofile_release` could determine if the `Node` were opened with
    // `OPEN_FLAG_NODE_REFERENCE` (by calling `Node.NodeGetFlags` or `File.GetFlags`).
    // Note that if `zxio_vmofile_release` starts calling `File.GetFlags`, `ServiceFs`
    // will need to stop unconditionally treating `Node`s opened with `OPEN_FLAG_NODE_REFERNCE`
    // as `Node`s.
    // TODO(fxb/40888)
    let pattern = format!(
        "/hub/r/{}/*/c/{}/*/out/diagnostics/root.i[n]spect",
        glob::Pattern::escape(nested_environment_label),
        component_name
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

    let vmo_file = File::open(path).expect("file exists");
    let vmo = fdio::get_vmo_copy_from_file(&vmo_file).expect("vmo exists");

    PartialNodeHierarchy::try_from(&vmo).unwrap().into()
}
