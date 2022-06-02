// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ProtocolMarker,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_sys as fsys,
    fuchsia_component::client::connect_to_protocol,
    futures::StreamExt,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::read_only_static, pseudo_directory,
    },
};

async fn launch_echo_component(
    start_info: fcrunner::ComponentStartInfo,
    realm_name: &str,
    execution_scope: ExecutionScope,
) -> Result<legacy_component_lib::LegacyComponent, Error> {
    let env =
        connect_to_protocol::<fsys::EnvironmentMarker>().expect("error connecting to environment");
    legacy_component_lib::LegacyComponent::run(
        "fuchsia-pkg://fuchsia.com/legacy_component_lib_tests#meta/echo_server.cmx".into(),
        start_info,
        env.into(),
        realm_name.into(),
        execution_scope,
    )
    .await
}

async fn call_echo(
    echo_str: Option<&str>,
    dir_proxy: &fio::DirectoryProxy,
) -> Result<Option<String>, Error> {
    let echo = fuchsia_component::client::connect_to_named_protocol_at_dir_root::<fecho::EchoMarker>(
        &dir_proxy,
        format!("svc/{}", fecho::EchoMarker::DEBUG_NAME).as_str(),
    )?;
    echo.echo_string(echo_str).await.map_err(|e| e.into())
}

#[fuchsia::test]
async fn test_legacy_echo() {
    let execution_scope = ExecutionScope::new();
    let mut start_info = fcrunner::ComponentStartInfo::EMPTY;
    start_info.ns = Some(vec![]);

    let (dir_proxy, dir_end) = fidl::endpoints::create_proxy().unwrap();
    start_info.outgoing_dir = Some(dir_end);

    let _component =
        launch_echo_component(start_info, "test_legacy_echo", execution_scope).await.unwrap();
    const ECHO_STRING: &str = "Hello, world!";
    let out = call_echo(Some(ECHO_STRING), &dir_proxy).await.expect("echo failed");
    assert_eq!(ECHO_STRING, out.unwrap());
}

#[fuchsia::test]
async fn test_legacy_echo_stop() {
    let execution_scope = ExecutionScope::new();
    let mut start_info = fcrunner::ComponentStartInfo::EMPTY;
    start_info.ns = Some(vec![]);

    let (dir_proxy, dir_end) = fidl::endpoints::create_proxy().unwrap();
    start_info.outgoing_dir = Some(dir_end);

    let component =
        launch_echo_component(start_info, "test_legacy_echo_stop", execution_scope.clone())
            .await
            .unwrap();
    let (proxy, stream) =
        fidl::endpoints::create_proxy_and_stream::<fcrunner::ComponentControllerMarker>().unwrap();
    let _controller_task =
        fuchsia_async::Task::local(component.serve_controller(stream, execution_scope));

    // make sure echo is running
    const ECHO_STRING: &str = "Hello, world!";
    let out = call_echo(Some(ECHO_STRING), &dir_proxy).await.expect("echo failed");
    assert_eq!(ECHO_STRING, out.unwrap());

    proxy.stop().unwrap();

    // wait till the channel is closed
    let mut stream = proxy.take_event_stream();
    while let Some(_) = stream.next().await {}

    // make sure echo fails
    call_echo(Some(ECHO_STRING), &dir_proxy).await.expect_err("echo should fail");
}

#[fuchsia::test]
async fn test_legacy_echo_kill() {
    let execution_scope = ExecutionScope::new();
    let mut start_info = fcrunner::ComponentStartInfo::EMPTY;
    start_info.ns = Some(vec![]);

    let (dir_proxy, dir_end) = fidl::endpoints::create_proxy().unwrap();
    start_info.outgoing_dir = Some(dir_end);

    let component =
        launch_echo_component(start_info, "test_legacy_echo_kill", execution_scope.clone())
            .await
            .unwrap();
    let (proxy, stream) =
        fidl::endpoints::create_proxy_and_stream::<fcrunner::ComponentControllerMarker>().unwrap();
    let _controller_task =
        fuchsia_async::Task::local(component.serve_controller(stream, execution_scope));

    // make sure echo is running
    const ECHO_STRING: &str = "Hello, world!";
    let out = call_echo(Some(ECHO_STRING), &dir_proxy).await.expect("echo failed");
    assert_eq!(ECHO_STRING, out.unwrap());

    proxy.kill().unwrap();

    // wait till the channel is closed
    let mut stream = proxy.take_event_stream();
    while let Some(_) = stream.next().await {}

    // make sure echo fails
    call_echo(Some(ECHO_STRING), &dir_proxy).await.expect_err("echo should fail");
}

#[fuchsia::test]
async fn test_legacy_echo_with_args() {
    const TEST_VALUE: &str = "TEST";

    let execution_scope = ExecutionScope::new();
    let mut start_info = fcrunner::ComponentStartInfo::EMPTY;
    start_info.ns = Some(vec![]);
    start_info.program = Some(fdata::Dictionary {
        entries: Some(vec![fdata::DictionaryEntry {
            key: "args".to_string(),
            value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![TEST_VALUE.to_string()]))),
        }]),
        ..fdata::Dictionary::EMPTY
    });

    let (dir_proxy, dir_end) = fidl::endpoints::create_proxy().unwrap();
    start_info.outgoing_dir = Some(dir_end);

    let _component =
        launch_echo_component(start_info, "test_legacy_echo_args", execution_scope).await.unwrap();
    let out = call_echo(None, &dir_proxy).await.expect("echo failed");
    assert_eq!(TEST_VALUE.to_string(), out.unwrap());
}

#[fuchsia::test]
async fn test_legacy_echo_with_directory() {
    const TEST_VALUE: &str = "TEST";

    let config_data_dir = pseudo_directory! {
        "default_reply.txt" => read_only_static(TEST_VALUE),
    };
    let (client_end, server_end) =
        fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    config_data_dir.open(
        scope,
        fio::OpenFlags::RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server_end.into_channel()),
    );

    let mut start_info = fcrunner::ComponentStartInfo::EMPTY;
    start_info.ns = Some(vec![fcrunner::ComponentNamespaceEntry {
        path: Some("/config".to_string()),
        directory: Some(client_end),
        ..fcrunner::ComponentNamespaceEntry::EMPTY
    }]);

    let (dir_proxy, dir_end) = fidl::endpoints::create_proxy().unwrap();
    start_info.outgoing_dir = Some(dir_end);

    let _component =
        launch_echo_component(start_info, "test_legacy_echo_dir", ExecutionScope::new())
            .await
            .unwrap();
    let out = call_echo(None, &dir_proxy).await.expect("echo failed");
    assert_eq!(TEST_VALUE.to_string(), out.unwrap());
}
