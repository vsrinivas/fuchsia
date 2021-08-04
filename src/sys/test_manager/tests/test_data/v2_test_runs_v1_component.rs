// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::create_proxy;
use fidl_fidl_examples_echo as fe;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_sys as fsys;
use fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_dir_root};
use futures::stream::TryStreamExt;

// This is an example test of bridging a v2 and v1 component.
// The test starts `echo_server_rust` and echoes the string "hello," however,
// the echo server is run in V1!
// Existing v1 test code can be used as it is.
#[fuchsia::test]
async fn launch_and_test_v1_component() {
    let launcher_proxy = connect_to_protocol::<fsys::LauncherMarker>().expect("failed to connect");
    let url = "fuchsia-pkg://fuchsia.com/test_manager_test#meta/echo_server_rust.cmx";
    let (directory, directory_request) =
        create_proxy::<fio::DirectoryMarker>().expect("create dir proxy");

    let mut launch_info = fsys::LaunchInfo {
        url: url.to_string(),
        arguments: None,
        out: None,
        err: None,
        directory_request: Some(directory_request.into_channel()),
        flat_namespace: None,
        additional_services: None,
    };

    let (component_controller, component_controller_server) =
        create_proxy::<fsys::ComponentControllerMarker>().expect("create component controller");

    launcher_proxy
        .create_component(&mut launch_info, Some(component_controller_server))
        .expect("create component");

    match component_controller.take_event_stream().try_next().await.expect("get event") {
        Some(fsys::ComponentControllerEvent::OnDirectoryReady { .. }) => {}
        e => {
            assert!(false, "Expected directory opened event, got {:?}", e);
        }
    }

    let echo_proxy =
        connect_to_protocol_at_dir_root::<fe::EchoMarker>(&directory).expect("connect to echo");

    assert_eq!(
        Some("hello".to_string()),
        echo_proxy.echo_string(Some("hello")).await.expect("echo string")
    );
}

#[fuchsia::test]
async fn launch_v1_logging_component() {
    let launcher_proxy = connect_to_protocol::<fsys::LauncherMarker>().expect("failed to connect");
    let url = "fuchsia-pkg://fuchsia.com/test_manager_test#meta/logging_component.cmx";

    let mut launch_info = fsys::LaunchInfo {
        url: url.to_string(),
        arguments: None,
        out: None,
        err: None,
        directory_request: None,
        flat_namespace: None,
        additional_services: None,
    };

    let (component_controller, component_controller_server) =
        create_proxy::<fsys::ComponentControllerMarker>().expect("create component controller");

    launcher_proxy
        .create_component(&mut launch_info, Some(component_controller_server))
        .expect("create component");

    let mut events = component_controller.take_event_stream();

    while let Some(event) = events.try_next().await.expect("get event") {
        match event {
            fsys::ComponentControllerEvent::OnTerminated { return_code, .. } => {
                assert_eq!(return_code, 0);
            }
            _ => {}
        }
    }
}

#[fuchsia::test]
async fn enclosing_env_services() {
    let env_proxy = connect_to_protocol::<fsys::EnvironmentMarker>().expect("failed to connect");
    let (dir_proxy, directory_request) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    env_proxy.get_directory(directory_request.into_channel()).unwrap();

    let protocols = files_async::readdir(&dir_proxy)
        .await
        .unwrap()
        .into_iter()
        .map(|d| d.name)
        .collect::<Vec<_>>();

    // make sure we are not getting access to any system services.
    assert_eq!(
        protocols,
        vec![
            "fuchsia.logger.LogSink".to_string(),
            "fuchsia.process.Launcher".to_string(),
            "fuchsia.process.Resolver".to_string(),
            "fuchsia.sys.Environment".to_string(),
            "fuchsia.sys.Launcher".to_string(),
            "fuchsia.sys.Loader".to_string(),
            "fuchsia.sys.internal.LogConnector".to_string()
        ]
    )
}
