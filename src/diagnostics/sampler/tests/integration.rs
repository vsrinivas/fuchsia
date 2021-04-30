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
        test_component.connect_to_protocol::<SamplerTestControllerMarker>().unwrap();

    let reboot_controller =
        fuchsia_component::client::connect_to_protocol::<MockRebootControllerMarker>().unwrap();

    let logger_querier = fuchsia_component::client::connect_to_protocol::<
        fidl_fuchsia_cobalt_test::LoggerQuerierMarker,
    >()
    .unwrap();

    (sampler_app, test_component, test_app_controller, logger_querier, reboot_controller)
}

/// Runs the Lapis Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses cobalt mock and log querier to
/// verify that the sampler observers changes as expected, and logs them to
/// cobalt as expected.
#[fuchsia::test]
async fn event_count_sampler_test() {
    let (mut sampler_app, _test_component, test_app_controller, logger_querier, reboot_controller) =
        setup().await;

    let sampler_app: fasync::Task<Result<(), Error>> = fasync::Task::spawn(async move {
        sampler_app.wait().await?;
        Ok(())
    });

    // If we don't sleep, then calls to logger_querier.watch fail because
    // the logger isnt available.
    // TODO(fxb/45331): Remove sleep when race is resolved.
    fasync::Timer::new(fasync::Time::after(2.seconds())).await;

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
    test_app_controller.increment_int(1).unwrap();

    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    let events = gather_sample_group(
        LogQuerierConfig { project_id: 5, expected_batch_size: 2 },
        &logger_querier,
    )
    .await;

    // Even though we incremented metric-1 its value stays at 1 since it's being cached.
    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 1, value: 1 }));
    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 2, value: 10 }));

    // trigger_reboot calls the on_reboot callback that drives lapis shutdown. this
    // should await until lapis has finished its cleanup, which means we should have some events
    // present when we're done, and the sampler task should be finished.
    reboot_controller.trigger_reboot().await.unwrap().unwrap();

    assert!(sampler_app.await.is_ok());

    let events = gather_sample_group(
        LogQuerierConfig { project_id: 5, expected_batch_size: 2 },
        &logger_querier,
    )
    .await;

    // The metric configured to run every 3000 seconds gets polled, and gets an undiffed
    // report of its values.
    assert!(verify_event_present_once(&events, ExpectedEvent { metric_id: 4, value: 2 }));
    // The integer metric which is always getting undiffed sampling is sampled one last time.
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
