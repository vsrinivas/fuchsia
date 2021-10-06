// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_test_components as ftest, fidl_fuchsia_data as fdata, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        mock::MockHandles,
        Realm,
    },
    futures::channel::mpsc,
    futures::prelude::*,
    log::*,
    std::env,
};

#[fuchsia::test]
async fn reboot_on_terminate_success() {
    let (send_trigger_called, mut receive_trigger_called) = mpsc::unbounded();
    let mut realm = build_reboot_on_terminate_realm(send_trigger_called).await;

    // TODO(fxbug.dev/86057): Use a relative URL when the component manager can load the manifest
    // from its own package.
    set_component_manager_url(
        &mut realm,
        &(env!("REBOOT_ON_TERMINATE_PACKAGE").to_owned() + "#meta/reboot_on_terminate_success.cm"),
    )
    .await;
    let _realm = realm.create().await.unwrap();

    // Wait for the test to signal that it received the shutdown request.
    info!("waiting for shutdown request");
    let _ = receive_trigger_called.next().await.expect("failed to receive results");
}

#[fuchsia::test]
async fn reboot_on_terminate_policy() {
    let (send_trigger_called, mut receive_trigger_called) = mpsc::unbounded();
    let mut realm = build_reboot_on_terminate_realm(send_trigger_called).await;

    // TODO(fxbug.dev/86057): Use a relative URL when the component manager can load the manifest
    // from its own package.
    set_component_manager_url(
        &mut realm,
        &(env!("REBOOT_ON_TERMINATE_PACKAGE").to_owned() + "#meta/reboot_on_terminate_policy.cm"),
    )
    .await;
    let _realm = realm.create().await.unwrap();

    // Wait for the test to signal that the security policy was correctly applied.
    info!("waiting for policy error");
    let _ = receive_trigger_called.next().await.expect("failed to receive results");
}

async fn build_reboot_on_terminate_realm(
    send_trigger_called: mpsc::UnboundedSender<()>,
) -> fuchsia_component_test::Realm {
    let mut builder = RealmBuilder::new().await.unwrap();

    // The actual test runs in a nested component manager that is configured with
    // reboot_on_terminate_enabled.
    builder
        .add_eager_component(
            "component_manager",
            ComponentSource::url("#meta/component_manager.cm"),
        )
        .await
        .unwrap();

    // The root component will use Trigger to report its shutdown.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.test.components.Trigger"),
            source: RouteEndpoint::component("trigger"),
            targets: vec![RouteEndpoint::component("component_manager")],
        })
        .unwrap();
    builder
        .add_component(
            "trigger",
            ComponentSource::mock(move |mock_handles| {
                Box::pin(trigger_mock(send_trigger_called.clone(), mock_handles))
            }),
        )
        .await
        .unwrap();

    // Forward logging to debug test breakages.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("component_manager")],
        })
        .unwrap();
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.boot.WriteOnlyLog"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("component_manager")],
        })
        .unwrap();

    // Forward loader so that nested component_manager can load packages.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys.Loader"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("component_manager")],
        })
        .unwrap();

    // Component manager needs fuchsia.process.Launcher to spawn new processes.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.process.Launcher"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("component_manager")],
        })
        .unwrap();

    return builder.build();
}

async fn trigger_mock(
    send_trigger_called: mpsc::UnboundedSender<()>,
    mock_handles: MockHandles,
) -> Result<(), anyhow::Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];
    fs.dir("svc").add_fidl_service(move |mut stream: ftest::TriggerRequestStream| {
        let mut send_trigger_called = send_trigger_called.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(ftest::TriggerRequest::Run { responder }) =
                stream.try_next().await.expect("failed to serve trigger service")
            {
                responder.send("received").expect("failed to send trigger response");
                send_trigger_called.send(()).await.expect("failed to send results");
            }
        }));
    });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

async fn set_component_manager_url(realm: &mut Realm, url: &str) {
    let mut cm_decl = realm.get_decl(&"component_manager".into()).await.unwrap();
    let program = cm_decl.program.as_mut().unwrap();
    program.info.entries.as_mut().unwrap().push(fdata::DictionaryEntry {
        key: "args".into(),
        value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![
            "--config".to_string(),
            "/pkg/data/component_manager_config".to_string(),
            url.to_string(),
        ]))),
    });
    realm.set_component(&"component_manager".into(), cm_decl).await.unwrap();
}
