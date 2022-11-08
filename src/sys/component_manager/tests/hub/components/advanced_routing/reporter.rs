// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_fs::OpenFlags;
use fuchsia_zircon as zx;
use hub_report::*;

#[fuchsia::main]
async fn main() {
    expect_dir_listing("/parent_hub/children", vec!["echo_server", "reporter"]).await;

    expect_file_content("/parent_hub/children/reporter/runtime/args/0", "Hippos").await;
    expect_file_content("/parent_hub/children/reporter/runtime/args/1", "rule!").await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/exposed",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.component.Binder", "hub"],
    )
    .await;

    expect_dir_listing("/parent_hub/children/echo_server/out", vec!["svc"]).await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/out/svc",
        vec!["fidl.examples.routing.echo.Echo"],
    )
    .await;

    expect_dir_listing_with_optionals(
        "/parent_hub/children/reporter/ns/svc",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.logger.LogSink"],
        // Coverage builds also use debugdata.Publisher
        vec!["fuchsia.debugdata.Publisher"],
    )
    .await;

    expect_echo_service("/parent_hub/children/echo_server/exposed/fidl.examples.routing.echo.Echo")
        .await;
    expect_echo_service("/parent_hub/children/echo_server/out/svc/fidl.examples.routing.echo.Echo")
        .await;
    expect_echo_service("/parent_hub/children/reporter/ns/svc/fidl.examples.routing.echo.Echo")
        .await;

    expect_dir_listing("/hub", vec!["children", "exposed", "ns", "out", "runtime"]).await;

    // Verify that the a read-only hub cannot be opened as RW.
    //
    // The call to `fuchsia_fs::directory::open_in_namespace` will not fail because fdio does not wait for
    // an OnOpen event. However the channel to the hub will still be closed with an `ACCESS_DENIED`
    // epitaph.
    //
    // We should be able to see that the channel is closed by trying to making a Describe call on it,
    // which should fail.
    let ro_hub = fuchsia_fs::directory::open_in_namespace(
        "/read_only_hub",
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .unwrap();
    let err = ro_hub.query().await.unwrap_err();
    match err {
        fidl::Error::ClientChannelClosed { status: zx::Status::ACCESS_DENIED, .. } => {}
        err => panic!("Unexpected error: {:?}", err),
    }
}
