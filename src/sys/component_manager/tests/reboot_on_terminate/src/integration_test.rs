// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_test_components as ftest, fidl_fuchsia_data as fdata, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::channel::mpsc,
    futures::prelude::*,
    std::env,
    tracing::*,
};

#[fuchsia::test]
async fn reboot_on_terminate_success() {
    let (send_trigger_called, mut receive_trigger_called) = mpsc::unbounded();
    let builder = build_reboot_on_terminate_realm(send_trigger_called).await;

    // TODO(fxbug.dev/86057): Use a relative URL when the component manager can load the manifest
    // from its own package.
    set_component_manager_url(
        &builder,
        &(env!("REBOOT_ON_TERMINATE_PACKAGE").to_owned() + "#meta/reboot_on_terminate_success.cm"),
    )
    .await;
    let _realm = builder.build().await.unwrap();

    // Wait for the test to signal that it received the shutdown request.
    info!("waiting for shutdown request");
    let _ = receive_trigger_called.next().await.expect("failed to receive results");
}

#[fuchsia::test]
async fn reboot_on_terminate_policy() {
    let (send_trigger_called, mut receive_trigger_called) = mpsc::unbounded();
    let builder = build_reboot_on_terminate_realm(send_trigger_called).await;

    // TODO(fxbug.dev/86057): Use a relative URL when the component manager can load the manifest
    // from its own package.
    set_component_manager_url(
        &builder,
        &(env!("REBOOT_ON_TERMINATE_PACKAGE").to_owned() + "#meta/reboot_on_terminate_policy.cm"),
    )
    .await;
    let _realm = builder.build().await.unwrap();

    // Wait for the test to signal that the security policy was correctly applied.
    info!("waiting for policy error");
    let _ = receive_trigger_called.next().await.expect("failed to receive results");
}

async fn build_reboot_on_terminate_realm(
    send_trigger_called: mpsc::UnboundedSender<()>,
) -> RealmBuilder {
    let builder = RealmBuilder::new().await.unwrap();

    // The actual test runs in a nested component manager that is configured with
    // reboot_on_terminate_enabled.
    let component_manager = builder
        .add_child("component_manager", "#meta/component_manager.cm", ChildOptions::new().eager())
        .await
        .unwrap();

    // The root component will use Trigger to report its shutdown.
    let trigger = builder
        .add_local_child(
            "trigger",
            move |handles| Box::pin(trigger_mock(send_trigger_called.clone(), handles)),
            ChildOptions::new(),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.test.components.Trigger"))
                .from(&trigger)
                .to(&component_manager),
        )
        .await
        .unwrap();

    builder
        .add_route(
            Route::new()
                // Forward logging to debug test breakages.
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.boot.WriteOnlyLog"))
                // Forward loader so that nested component_manager can load packages.
                .capability(Capability::protocol_by_name("fuchsia.sys.Loader"))
                // Component manager needs fuchsia.process.Launcher to spawn new processes.
                .capability(Capability::protocol_by_name("fuchsia.process.Launcher"))
                .from(Ref::parent())
                .to(&component_manager),
        )
        .await
        .unwrap();

    builder
}

async fn trigger_mock(
    send_trigger_called: mpsc::UnboundedSender<()>,
    handles: LocalComponentHandles,
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
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

async fn set_component_manager_url(builder: &RealmBuilder, url: &str) {
    let mut cm_decl = builder.get_component_decl("component_manager").await.unwrap();
    let program = cm_decl.program.as_mut().unwrap();
    program.info.entries.as_mut().unwrap().push(fdata::DictionaryEntry {
        key: "args".into(),
        value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![
            "--config".to_string(),
            "/pkg/data/component_manager_config".to_string(),
            url.to_string(),
        ]))),
    });
    builder.replace_component_decl("component_manager", cm_decl).await.unwrap();
}
