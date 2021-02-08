// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use diagnostics_hierarchy::DiagnosticsHierarchy;
use diagnostics_reader::{ArchiveReader, Inspect};
use fidl_fuchsia_mockrebootcontroller::{MockRebootControllerMarker, MockRebootControllerProxy};
use fidl_fuchsia_samplertestcontroller::{SamplerTestControllerMarker, SamplerTestControllerProxy};
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher, App};
use fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty};
use fuchsia_zircon::DurationNum;

async fn setup() -> (App, App, SamplerTestControllerProxy, MockRebootControllerProxy) {
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

    (sampler_app, test_component, test_app_controller, reboot_controller)
}

async fn sampler_inspect_hierarchy() -> DiagnosticsHierarchy {
    let mut data = ArchiveReader::new()
        .add_selector("sampler.cmx:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    data.pop().expect("one result").payload.expect("payload is not none")
}

/// Runs the Lapis Sampler and a test component that can have its inspect properties
/// manipulated by the test via fidl, and uses mock services to determine that when
/// the reboot server goes down, lapis continues to run as expected.
#[fasync::run_singlethreaded(test)]
async fn sampler_inspect_test() {
    let (mut sampler_app, _test_component, test_app_controller, _reboot_controller) = setup().await;

    let _sampler_app: fasync::Task<Result<(), Error>> = fasync::Task::spawn(async move {
        sampler_app.wait().await.unwrap();
        panic!("we crash the reboot server, so sampler_app should never exit.")
    });

    // If we don't sleep, then calls to logger_querier.watch fail because
    // the logger isnt available.
    // TODO(fxb/45331): Remove sleep when race is resolved.
    fasync::Timer::new(fasync::Time::after(2.seconds())).await;

    test_app_controller.increment_int(1).unwrap();

    // We want to guarantee a sample takes place before we check inspect stuff.
    test_app_controller.wait_for_sample().await.unwrap().unwrap();

    // Observe verification shows up in inspect.
    let hierarchy = sampler_inspect_hierarchy().await;

    assert_inspect_tree!(
        hierarchy,
        root: {
            "sampler_executor_stats": {
                "healthily_exited_samplers": 0 as u64,
                "errorfully_exited_samplers": 0 as u64,
                "reboot_exited_samplers": 0 as u64,
                "total_project_samplers_configured": 2 as u64,
                "project_5": {
                    "project_sampler_count": 2 as u64,
                    "metrics_configured": 4 as u64,
                    "cobalt_logs_sent": 3 as u64,
                }
            },
            "fuchsia.inspect.Health": {
                "start_timestamp_nanos": AnyProperty,
                "status": "OK"
            }
        }
    );
}
