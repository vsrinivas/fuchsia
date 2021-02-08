// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use fidl_fuchsia_cobalt::{CobaltEvent, EventPayload};
use fidl_fuchsia_cobalt_test::{self, LoggerQuerierProxy};
use fidl_fuchsia_mockrebootcontroller::{MockRebootControllerMarker, MockRebootControllerProxy};
use fidl_fuchsia_samplertestcontroller::{SamplerTestControllerMarker, SamplerTestControllerProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher, App};
use fuchsia_zircon::DurationNum;

async fn setup(
) -> (App, App, SamplerTestControllerProxy, LoggerQuerierProxy, MockRebootControllerProxy) {
    let package = "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/";
    let sampler_manifest = "sampler.cmx";
    let test_component_manifest = "single_counter_test_component.cmx";

    let sampler_url = format!("{}{}", package, sampler_manifest);
    let test_component_url = format!("{}{}", package, test_component_manifest);

    let sampler_app = launch(&launcher().unwrap(), sampler_url.to_string(), None).unwrap();

    let test_component =
        launch(&launcher().unwrap(), test_component_url.to_string(), None).unwrap();

    let test_app_controller =
        test_component.connect_to_service::<SamplerTestControllerMarker>().unwrap();

    let reboot_controller =
        fuchsia_component::client::connect_to_service::<MockRebootControllerMarker>().unwrap();

    let logger_querier = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_cobalt_test::LoggerQuerierMarker,
    >()
    .unwrap();

    (sampler_app, test_component, test_app_controller, logger_querier, reboot_controller)
}

/// Runs the Lapis Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses mock services to determine that when
/// the reboot server goes down, lapis continues to run as expected.
#[fasync::run_singlethreaded(test)]
async fn reboot_server_crashed_test() {
    let (mut sampler_app, _test_component, test_app_controller, logger_querier, reboot_controller) =
        setup().await;

    let _sampler_app: fasync::Task<Result<(), Error>> = fasync::Task::spawn(async move {
        sampler_app.wait().await.unwrap();
        panic!("we crash the reboot server, so sampler_app should never exit.")
    });

    // If we don't sleep, then calls to logger_querier.watch fail because
    // the logger isnt available.
    // TODO(fxb/45331): Remove sleep when race is resolved.
    fasync::Timer::new(fasync::Time::after(2.seconds())).await;

    // Crash the reboot server to verify that lapis continues to sample.
    reboot_controller.crash_reboot_channel().await.unwrap().unwrap();

    test_app_controller.increment_int(1).unwrap();

    let events = gather_sample_group(
        LogQuerierConfig { project_id: 5, expected_batch_size: 3 },
        &logger_querier,
    )
    .await;

    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 1, value: 1 }));
    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 2, value: 10 }));
    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 3, value: 20 }));

    // We want to guarantee a sample takes place before we increment the value again.
    // This is to verify that despite two samples taking place, the count type isnt uploaded with no diff
    // and the metric that is upload_once isnt sampled again.
    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    let events = gather_sample_group(
        LogQuerierConfig { project_id: 5, expected_batch_size: 1 },
        &logger_querier,
    )
    .await;

    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 2, value: 10 }));
}

struct ExpectedEvent {
    metric_id: u32,
    value: i64,
}

fn verify_event_present_once(events: &Vec<CobaltEvent>, expected_event: ExpectedEvent) -> bool {
    let mut observed_count = 0;
    for event in events {
        match event.payload {
            EventPayload::EventCount(event_count) => {
                if event.metric_id == expected_event.metric_id
                    && event_count.count == expected_event.value
                {
                    observed_count += 1;
                }
            }
            _ => panic!("Only should be observing CountEvents."),
        }
    }
    observed_count == 1
}

struct LogQuerierConfig {
    project_id: u32,
    expected_batch_size: usize,
}

async fn gather_sample_group(
    log_querier_config: LogQuerierConfig,
    logger_querier: &LoggerQuerierProxy,
) -> Vec<CobaltEvent> {
    let mut events: Vec<CobaltEvent> = Vec::new();
    loop {
        let watch = logger_querier.watch_logs(
            log_querier_config.project_id,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvent,
        );

        let (mut new_events, more) = watch.await.unwrap().unwrap();
        assert!(!more);

        events.append(&mut new_events);
        if events.len() < log_querier_config.expected_batch_size {
            continue;
        }

        if events.len() == log_querier_config.expected_batch_size {
            break;
        }

        panic!("Sampler should provide descrete groups of cobalt events. Shouldn't see more than {:?} here.", log_querier_config.expected_batch_size);
    }

    events
}
