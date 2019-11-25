// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error, fidl_fidl_examples_echo as fidl_echo, fidl_fuchsia_test_breakpoints as fbreak,
    fuchsia_async as fasync, fuchsia_component::client::*, io_util, test_utils::*,
};

#[fasync::run_singlethreaded(test)]
async fn base_resolver_test() -> Result<(), Error> {
    // Obtain access to this component's pkg directory
    let pkg_proxy = io_util::open_directory_in_namespace("/pkg", io_util::OPEN_RIGHT_READABLE)?;
    let pkg_channel = pkg_proxy.into_channel().unwrap().into_zx_channel();

    // A custom BlackBoxTest is required because
    // 1. the /pkg dir of this component has to be passed in to component manager as /boot
    // 2. the component manager needs a manifest without fuchsia.sys.Loader
    let test = BlackBoxTest::custom(
        "fuchsia-pkg://fuchsia.com/base_resolver_test#meta/component_manager_without_loader.cmx",
        "fuchsia-boot:///#meta/root.cm",
        vec![("/boot".to_string(), pkg_channel.into())],
    )
    .await?;

    // Register breakpoints and begin execution of component manager
    let receiver = test.breakpoint_system.register(vec![fbreak::EventType::StartInstance]).await?;

    // Expect the root component to be bound to
    let invocation = receiver.expect(fbreak::EventType::StartInstance, vec![]).await?;
    invocation.resume().await?;

    // Expect the echo_server component to be bound to
    let invocation =
        receiver.expect(fbreak::EventType::StartInstance, vec!["echo_server:0"]).await?;
    invocation.resume().await?;

    // Connect to the echo service
    let path_to_service_dir =
        test.component_manager_path.join("out/hub/children/echo_server/exec/out/svc");
    let path_to_service_dir = path_to_service_dir.to_str().expect("unexpected chars");
    let echo_proxy = connect_to_service_at::<fidl_echo::EchoMarker>(path_to_service_dir)?;

    // Test the echo service
    assert_eq!(Some("hippos!".to_string()), echo_proxy.echo_string(Some("hippos!")).await?);

    Ok(())
}
