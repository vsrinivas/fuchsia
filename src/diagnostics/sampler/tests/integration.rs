// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl_fuchsia_cobalt::{CobaltEvent, EventPayload};
use fidl_fuchsia_cobalt_test;
use fidl_fuchsia_samplertestcontroller::SamplerTestControllerMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use fuchsia_zircon::DurationNum;

/// Runs the Lapis Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses cobalt mock and log querier to
/// verify that the sampler observers changes as expected, and logs them to
/// cobalt as expected.
#[fasync::run_singlethreaded(test)]
async fn event_count_sampler_test() {
    let package = "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/";
    let sampler_manifest = "sampler.cmx";
    let test_component_manifest = "single_counter_test_component.cmx";

    let sampler_url = format!("{}{}", package, sampler_manifest);
    let test_component_url = format!("{}{}", package, test_component_manifest);

    let mut sampler_app = launch(&launcher().unwrap(), sampler_url.to_string(), None).unwrap();
    let _sampler_app = fasync::Task::spawn(async move {
        sampler_app.wait().await.unwrap();
        panic!("sampler should not exit during test!");
    });

    let test_component =
        launch(&launcher().unwrap(), test_component_url.to_string(), None).unwrap();
    let test_app_controller =
        test_component.connect_to_service::<SamplerTestControllerMarker>().unwrap();

    let logger_querier = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_cobalt_test::LoggerQuerierMarker,
    >()
    .unwrap();

    // If we don't sleep, then calls to logger_querier.watch fail because
    // the logger isnt available.
    // TODO(fxb/45331): Remove sleep when race is resolved.
    fasync::Timer::new(fasync::Time::after(2.seconds())).await;

    test_app_controller.increment_int(1).unwrap();

    let watch = logger_querier.watch_logs(5, fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvent);

    let (mut events, more) = watch.await.unwrap().unwrap();

    assert!(!more);
    assert_eq!(events.len(), 1);

    let cobalt_event: CobaltEvent = events.pop().unwrap();

    assert_eq!(cobalt_event.metric_id, 1);
    match cobalt_event.payload {
        EventPayload::EventCount(event_count) => {
            assert_eq!(event_count.count, 1);
        }
        _ => panic!("Only should be observing CountEvents."),
    }

    // We want to guarantee a sample takes place before we increment the value again.
    // This is to verify that despite two samples taking place, only one new log is emitted.
    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    test_app_controller.increment_int(1).unwrap();

    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    let watch = logger_querier.watch_logs(5, fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvent);

    let (mut events, more) = watch.await.unwrap().unwrap();

    assert!(!more);
    assert_eq!(events.len(), 1);

    let cobalt_event: CobaltEvent = events.pop().unwrap();

    // Count stays at 1, even though the backing inspect property
    // is now at 2.
    assert_eq!(cobalt_event.metric_id, 1);
    match cobalt_event.payload {
        EventPayload::EventCount(event_count) => {
            assert_eq!(event_count.count, 1);
        }
        _ => panic!("Only should be observing CountEvents."),
    }
}
