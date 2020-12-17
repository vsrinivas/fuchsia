// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryMarker, MODE_TYPE_DIRECTORY, OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_test_pkg_reflector::ReflectorMarker,
    fidl_test_pkg_thinger::ThingerMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_syslog::fx_log_info,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::pcb::read_only_static, mut_pseudo_directory, path::Path,
    },
};

const TEST_CASE_REALM: &str =
    "fuchsia-pkg://fuchsia.com/dir-reflector-integration-test#meta/test-case-realm.cm";

#[fasync::run_singlethreaded(test)]
async fn test() {
    fuchsia_syslog::init_with_tags(&["dir-reflector", "driver"])
        .expect("syslog init should not fail");
    test_inner().await;
}

async fn test_inner() {
    fx_log_info!("creating fake pkgfs");

    let pkgfs = mut_pseudo_directory! {
        "a" => read_only_static(""),
        "b" => mut_pseudo_directory! {
            "c" => read_only_static(""),
        },
    };
    let (pkgfs_client_end, pkgfs_server_end) =
        fidl::endpoints::create_endpoints::<DirectoryMarker>().expect("creating pkgfs channel");

    pkgfs.open(
        ExecutionScope::new(),
        OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        MODE_TYPE_DIRECTORY,
        Path::empty(),
        ServerEnd::new(pkgfs_server_end.into_channel()),
    );

    fx_log_info!("launching pkg-cache component tree");

    let app = ScopedInstance::new("coll".to_string(), TEST_CASE_REALM.to_string())
        .await
        .expect("failed to create test component");

    fx_log_info!("registering fake pkgfs");

    let reflector = app
        .connect_to_protocol_at_exposed_dir::<ReflectorMarker>()
        .expect("connecting to reflector");

    reflector.reflect(pkgfs_client_end).await.expect("reflect");

    fx_log_info!("connecting to thinger");

    let thinger =
        app.connect_to_protocol_at_exposed_dir::<ThingerMarker>().expect("connecting to thinger");

    fx_log_info!("doing the thing");
    assert_eq!(thinger.do_thing().await.expect("do_thing"), vec!["a", "b/c"],);

    fx_log_info!("doing the thing again to make sure we can re-open the directory");
    assert_eq!(thinger.do_thing().await.expect("do_thing"), vec!["a", "b/c"],);
}
