// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{DebugStarted, EventStream, Stopped},
        matcher::EventMatcher,
    },
    fidl_fuchsia_io as fio,
    fuchsia_async::{Duration, Task, Time, Timer},
    fuchsia_component_test::ScopedInstance,
    fuchsia_fs::directory::open_file_no_describe,
    std::mem,
};

#[fuchsia::test]
async fn test_debug_started() {
    let mut event_stream = EventStream::open().await.unwrap();
    let collection_name = "test-collection";

    // ScopedInstance kills the component when it goes outside of the scope.
    let instance = ScopedInstance::new(
        collection_name.to_owned(),
        "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/immediate_exit_component.cm"
            .to_owned(),
    )
    .await
    .unwrap();

    // connect_to_binder starts the component. Otherwise the component is only declared.
    instance.connect_to_binder().unwrap();

    let start = Time::now();

    let moniker = format!("./{}:{}", collection_name, instance.child_name());

    let event = EventMatcher::ok()
        .moniker(moniker.clone())
        .wait::<DebugStarted>(&mut event_stream)
        .await
        .expect("failed to observe events");

    let payload = event.result().expect("debug_started should not be err");

    let job_id_content =
        open_file_no_describe(&payload.runtime_dir, "elf/job_id", fio::OpenFlags::RIGHT_READABLE)
            .expect("cannot open elf/job_id")
            .read(fio::MAX_BUF)
            .await
            .expect("failed to read elf/job_id")
            .expect("failed to read elf/job_id");

    let job_id: u64 = String::from_utf8(job_id_content)
        .expect("cannot parse job_id")
        .parse()
        .expect("cannot parse job_id");

    // Ideally we want to get the job handle from the job_id, and install an exception channel
    // to check that break_on_start really works. But that requires access to RootJob which is
    // not available for this test.
    println!("job_id: {}", job_id);

    // Instead, we depends on the fact that the component exits immediately and check the arrival
    // time of the Stopped event.
    let _task = Task::spawn(async move {
        // Drop break_on_start after 20 milliseconds.
        Timer::new(Duration::from_millis(20)).await;

        // We only need to drop payload.break_on_start but it's a reference.
        mem::drop(event);
    });

    let _stoppped_event = EventMatcher::ok()
        .moniker(moniker)
        .wait::<Stopped>(&mut event_stream)
        .await
        .expect("failed to observe events");

    let elapsed = (Time::now() - start).into_millis();
    // around 29 ms, but could be arbitrarily large so we don't check the upper limit of it.
    println!("elapsed: {}", elapsed);
    assert!(elapsed >= 20);
}

#[fuchsia::test]
async fn test_debug_started_with_timeout() {
    let mut event_stream = EventStream::open().await.unwrap();
    let collection_name = "test-collection";

    // ScopedInstance kills the component when it goes outside of the scope.
    let instance = ScopedInstance::new(
        collection_name.to_owned(),
        "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/immediate_exit_component.cm"
            .to_owned(),
    )
    .await
    .unwrap();

    // connect_to_binder starts the component. Otherwise the component is only declared.
    instance.connect_to_binder().unwrap();

    let moniker = format!("./{}:{}", collection_name, instance.child_name());

    let _debug_started_event = EventMatcher::ok()
        .moniker(moniker.clone())
        .wait::<DebugStarted>(&mut event_stream)
        .await
        .expect("failed to observe events");

    // Hold the debug_started_event indefinitely to mimic a buggy debugger.
    // The component should still run and exit after certain timeout.

    let _stopped_event = EventMatcher::ok()
        .moniker(moniker)
        .wait::<Stopped>(&mut event_stream)
        .await
        .expect("failed to observe events");
}
