// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_async as fasync, hub_report::*};

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().unwrap();

    expect_dir_listing("/parent_hub/children", vec!["echo_server", "reporter"]).await;

    expect_file_content("/parent_hub/children/reporter/exec/runtime/args/0", "Hippos").await;
    expect_file_content("/parent_hub/children/reporter/exec/runtime/args/1", "rule!").await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/exec/expose",
        vec!["diagnostics", "fidl.examples.routing.echo.Echo", "hub"],
    )
    .await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/resolved/expose",
        vec!["diagnostics", "fidl.examples.routing.echo.Echo", "hub"],
    )
    .await;

    expect_dir_listing("/parent_hub/children/echo_server/exec/out", vec!["diagnostics", "svc"])
        .await;

    expect_dir_listing(
        "/parent_hub/children/echo_server/exec/out/svc",
        vec!["fidl.examples.routing.echo.Echo"],
    )
    .await;

    expect_dir_listing_with_optionals(
        "/parent_hub/children/reporter/exec/in/svc",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.logger.LogSink"],
        // Coverage builds also use DebugData
        vec!["fuchsia.debugdata.DebugData"],
    )
    .await;

    expect_dir_listing_with_optionals(
        "/parent_hub/children/reporter/resolved/use/svc",
        vec!["fidl.examples.routing.echo.Echo", "fuchsia.logger.LogSink"],
        // Coverage builds also use DebugData
        vec!["fuchsia.debugdata.DebugData"],
    )
    .await;

    expect_dir_listing("/parent_hub/children/reporter/exec/in/pkg", vec!["bin", "lib", "meta"])
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

    expect_dir_listing("/hub", vec!["expose", "in", "out", "resolved_url", "runtime"]).await;

    expect_file_content(
        "/sibling_hub/exec/resolved_url",
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_server.cm",
    )
    .await;
}
