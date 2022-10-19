// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_fs::OpenFlags;
use fuchsia_zircon as zx;
use hub_report::*;

#[fuchsia::main]
async fn main() {
    expect_dir_listing("/parent_hub/children", vec!["echo_server", "reporter"]).await;

    expect_file_content("/parent_hub/children/reporter/exec/runtime/args/0", "Hippos").await;
    expect_file_content("/parent_hub/children/reporter/exec/runtime/args/1", "rule!").await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/exec/expose",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.component.Binder", "hub"],
    )
    .await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/resolved/expose",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.component.Binder", "hub"],
    )
    .await;

    expect_dir_listing("/parent_hub/children/echo_server/exec/out", vec!["svc"]).await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/exec/out/svc",
        vec!["fidl.examples.routing.echo.Echo"],
    )
    .await;

    expect_dir_listing_with_optionals(
        "/parent_hub/children/reporter/exec/in/svc",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.logger.LogSink"],
        // Coverage builds also use debugdata.Publisher
        vec!["fuchsia.debugdata.Publisher"],
    )
    .await;

    expect_dir_listing_with_optionals(
        "/parent_hub/children/reporter/resolved/use/svc",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.logger.LogSink"],
        // Coverage builds also use debugdata.Publisher
        vec!["fuchsia.debugdata.Publisher"],
    )
    .await;

    expect_dir_listing(
        "/parent_hub/children/reporter/exec/in/pkg",
        vec!["bin", "data", "lib", "meta"],
    )
    .await;

    expect_echo_service(
        "/parent_hub/children/echo_server/resolved/expose/fidl.examples.routing.echo.Echo",
    )
    .await;
    expect_echo_service(
        "/parent_hub/children/echo_server/exec/expose/fidl.examples.routing.echo.Echo",
    )
    .await;
    expect_echo_service(
        "/parent_hub/children/echo_server/exec/out/svc/fidl.examples.routing.echo.Echo",
    )
    .await;
    expect_echo_service(
        "/parent_hub/children/reporter/exec/in/svc/fidl.examples.routing.echo.Echo",
    )
    .await;
    expect_echo_service(
        "/parent_hub/children/reporter/resolved/use/svc/fidl.examples.routing.echo.Echo",
    )
    .await;

    expect_dir_listing(
        "/hub",
        vec!["expose", "in", "out", "resolved_url", "runtime", "start_reason"],
    )
    .await;

    expect_file_content("/hub/start_reason", "Instance is an eager child").await;

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
