// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::{channel::mpsc, SinkExt, StreamExt, TryStreamExt},
};

async fn crash_receiver(
    handles: LocalComponentHandles,
    expected_crash_info: fsys::ComponentCrashInfo,
    mut success_reporter: mpsc::Sender<()>,
) -> Result<(), Error> {
    let crash_introspect_proxy = handles.connect_to_protocol::<fsys::CrashIntrospectMarker>()?;

    let (thread_koid_sender, mut thread_koid_receiver) = mpsc::channel(1);

    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |mut stream: ftest::ThreadKoidReporterRequestStream| {
        let mut thread_koid_sender = thread_koid_sender.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(ftest::ThreadKoidReporterRequest::ReportMyThreadKoid {
                thread_koid,
                ..
            }) =
                stream.try_next().await.expect("failed to serve thread koid reporting service")
            {
                thread_koid_sender.send(thread_koid).await.expect("failed to send thread koid");
            }
        }));
    });
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    let _fs_task = fasync::Task::local(fs.collect::<()>());

    let thread_koid = thread_koid_receiver.next().await.expect("failed to receive thread koid");

    // We have the thread koid of our sibling component, who will now crash. We don't know how long
    // it will take it to crash, so let's keep asking component manager in a loop until we find the
    // crash's information

    loop {
        match crash_introspect_proxy
            .find_component_by_thread_koid(thread_koid)
            .await
            .expect("failed to ask for crash information")
        {
            Ok(component_crash_info) => {
                // We found the crash information! If it matches what we're expecting, then the
                // test passes.
                assert_eq!(expected_crash_info, component_crash_info);
                success_reporter.send(()).await.expect("failed to report success");
                return Ok(());
            }
            Err(_) => {
                fasync::Timer::new(std::time::Duration::from_millis(200)).await;
            }
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn crashed_component_generates_a_record() -> Result<(), Error> {
    let expected_crash_info = fsys::ComponentCrashInfo {
        url: Some(
            "fuchsia-pkg://fuchsia.com/crash-introspect-test#meta/report_then_panic_on_start.cm"
                .to_string(),
        ),
        moniker: Some("/report_then_panic_on_start".to_string()),
        ..fsys::ComponentCrashInfo::EMPTY
    };
    let (success_sender, mut success_receiver) = mpsc::channel(1);
    let builder = RealmBuilder::new().await?;
    let crash_receiver = builder
        .add_local_child(
            "crash_receiver",
            move |mh| {
                Box::pin(crash_receiver(mh, expected_crash_info.clone(), success_sender.clone()))
            },
            ChildOptions::new(),
        )
        .await?;
    let report_then_panic_on_start = builder
        .add_child(
            "report_then_panic_on_start",
            "fuchsia-pkg://fuchsia.com/crash-introspect-test#meta/report_then_panic_on_start.cm",
            ChildOptions::new().eager(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&crash_receiver)
                .to(&report_then_panic_on_start),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.sys2.CrashIntrospect"))
                .from(Ref::parent())
                .to(&crash_receiver),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.test.ThreadKoidReporter"))
                .from(&crash_receiver)
                .to(&report_then_panic_on_start),
        )
        .await?;
    let _realm_instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await?;

    assert!(success_receiver.next().await.is_some());
    Ok(())
}
