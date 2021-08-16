// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_reader::{assert_data_tree, AnyProperty, ArchiveReader, Inspect};
use fidl_fuchsia_cobalt_test::LoggerQuerierMarker;
use fidl_fuchsia_component::BinderMarker;
use fidl_fuchsia_mockrebootcontroller::MockRebootControllerMarker;
use fidl_fuchsia_samplertestcontroller::SamplerTestControllerMarker;
use fuchsia_async as fasync;
use fuchsia_zircon::DurationNum;

mod mocks;
mod test_topology;
mod utils;

/// Runs the Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses cobalt mock and log querier to
/// verify that the sampler observers changes as expected, and logs them to
/// cobalt as expected.
#[fuchsia::test]
async fn event_count_sampler_test() {
    let instance = test_topology::create().await.expect("initialized topology");
    let test_app_controller =
        instance.root.connect_to_protocol_at_exposed_dir::<SamplerTestControllerMarker>().unwrap();
    let reboot_controller =
        instance.root.connect_to_protocol_at_exposed_dir::<MockRebootControllerMarker>().unwrap();
    let logger_querier =
        instance.root.connect_to_protocol_at_exposed_dir::<LoggerQuerierMarker>().unwrap();
    let _sampler_binder = instance
        .root
        .connect_to_named_protocol_at_exposed_dir::<BinderMarker>("fuchsia.component.SamplerBinder")
        .unwrap();

    // If we don't sleep, then calls to logger_querier.watch fail because
    // the logger isn't available.
    // TODO(fxb/45331): Remove sleep when race is resolved.
    fasync::Timer::new(fasync::Time::after(2.seconds())).await;

    test_app_controller.increment_int(1).unwrap();

    let events = utils::gather_sample_group(
        utils::LogQuerierConfig { project_id: 5, expected_batch_size: 3 },
        &logger_querier,
    )
    .await;

    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 1, value: 1 }
    ));
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 2, value: 10 }
    ));
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 3, value: 20 }
    ));

    // We want to guarantee a sample takes place before we increment the value again.
    // This is to verify that despite two samples taking place, the count type isn't uploaded with no diff
    // and the metric that is upload_once isn't sampled again.
    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    let events = utils::gather_sample_group(
        utils::LogQuerierConfig { project_id: 5, expected_batch_size: 1 },
        &logger_querier,
    )
    .await;

    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 2, value: 10 }
    ));
    test_app_controller.increment_int(1).unwrap();

    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    let events = utils::gather_sample_group(
        utils::LogQuerierConfig { project_id: 5, expected_batch_size: 2 },
        &logger_querier,
    )
    .await;

    // Even though we incremented metric-1 its value stays at 1 since it's being cached.
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 1, value: 1 }
    ));
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 2, value: 10 }
    ));

    // trigger_reboot calls the on_reboot callback that drives sampler shutdown. this
    // should await until sampler has finished its cleanup, which means we should have some events
    // present when we're done, and the sampler task should be finished.
    reboot_controller.trigger_reboot().await.unwrap().unwrap();

    let events = utils::gather_sample_group(
        utils::LogQuerierConfig { project_id: 5, expected_batch_size: 2 },
        &logger_querier,
    )
    .await;

    // The metric configured to run every 3000 seconds gets polled, and gets an undiffed
    // report of its values.
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 4, value: 2 }
    ));
    // The integer metric which is always getting undiffed sampling is sampled one last time.
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 2, value: 10 }
    ));
}

/// Runs the Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses mock services to determine that when
/// the reboot server goes down, sampler continues to run as expected.
#[fuchsia::test]
async fn reboot_server_crashed_test() {
    let instance = test_topology::create().await.expect("initialized topology");
    let test_app_controller =
        instance.root.connect_to_protocol_at_exposed_dir::<SamplerTestControllerMarker>().unwrap();
    let reboot_controller =
        instance.root.connect_to_protocol_at_exposed_dir::<MockRebootControllerMarker>().unwrap();
    let logger_querier =
        instance.root.connect_to_protocol_at_exposed_dir::<LoggerQuerierMarker>().unwrap();
    let _sampler_binder = instance
        .root
        .connect_to_named_protocol_at_exposed_dir::<BinderMarker>("fuchsia.component.SamplerBinder")
        .unwrap();

    // If we don't sleep, then calls to logger_querier.watch fail because
    // the logger isn't available.
    // TODO(fxb/45331): Remove sleep when race is resolved.
    fasync::Timer::new(fasync::Time::after(2.seconds())).await;

    // Crash the reboot server to verify that sampler continues to sample.
    reboot_controller.crash_reboot_channel().await.unwrap().unwrap();

    test_app_controller.increment_int(1).unwrap();

    let events = utils::gather_sample_group(
        utils::LogQuerierConfig { project_id: 5, expected_batch_size: 3 },
        &logger_querier,
    )
    .await;

    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 1, value: 1 }
    ));
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 2, value: 10 }
    ));
    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 3, value: 20 }
    ));

    // We want to guarantee a sample takes place before we increment the value again.
    // This is to verify that despite two samples taking place, the count type isn't uploaded with
    // no diff and the metric that is upload_once isn't sampled again.
    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    let events = utils::gather_sample_group(
        utils::LogQuerierConfig { project_id: 5, expected_batch_size: 1 },
        &logger_querier,
    )
    .await;

    assert!(utils::verify_event_present_once(
        &events,
        utils::ExpectedEvent { metric_id: 2, value: 10 }
    ));
}

/// Runs the Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses mock services to determine that when
/// the reboot server goes down, sampler continues to run as expected.
#[fuchsia::test]
async fn sampler_inspect_test() {
    let instance = test_topology::create().await.expect("initialized topology");
    let _sampler_binder = instance
        .root
        .connect_to_named_protocol_at_exposed_dir::<BinderMarker>("fuchsia.component.SamplerBinder")
        .unwrap();

    // Observe verification shows up in inspect.
    let mut data = ArchiveReader::new()
        .add_selector(format!(
            "fuchsia_component_test_collection\\:{}/wrapper/sampler:root",
            instance.root.child_name()
        ))
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    let hierarchy = data.pop().expect("one result").payload.expect("payload is not none");

    // TODO(42067): Introduce better fencing so we can
    // guarantee we fetch the hierarchy after the metrics were sampled
    // AND fully processed.
    assert_data_tree!(
        hierarchy,
        root: {
            sampler_executor_stats: {
                healthily_exited_samplers: 0 as u64,
                errorfully_exited_samplers: 0 as u64,
                reboot_exited_samplers: 0 as u64,
                total_project_samplers_configured: 2 as u64,
                project_5: {
                    project_sampler_count: 2 as u64,
                    metrics_configured: 5 as u64,
                    cobalt_logs_sent: AnyProperty,
                }
            },
            "fuchsia.inspect.Health": {
                start_timestamp_nanos: AnyProperty,
                status: AnyProperty
            }
        }
    );
}
