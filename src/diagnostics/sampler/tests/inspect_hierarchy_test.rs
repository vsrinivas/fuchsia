// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use diagnostics_hierarchy::DiagnosticsHierarchy;
use diagnostics_reader::{ArchiveReader, Inspect};
use fidl_fuchsia_mockrebootcontroller::{MockRebootControllerMarker, MockRebootControllerProxy};
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_async as fasync;
use fuchsia_async::futures::StreamExt;
use fuchsia_component::client::{launch, launcher, App};
use fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty};

async fn setup() -> (App, App, MockRebootControllerProxy) {
    let package = "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/";
    let sampler_manifest = "sampler.cmx";
    let test_component_manifest = "single_counter_test_component.cmx";

    let sampler_url = format!("{}{}", package, sampler_manifest);
    let test_component_url = format!("{}{}", package, test_component_manifest);

    let sampler_app = launch(&launcher().unwrap(), sampler_url.to_string(), None).unwrap();

    let test_component =
        launch(&launcher().unwrap(), test_component_url.to_string(), None).unwrap();

    let mut component_stream = test_component.controller().take_event_stream();

    match component_stream
        .next()
        .await
        .expect("component event stream has ended before termination event")
        .unwrap()
    {
        ComponentControllerEvent::OnDirectoryReady {} => {}
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            panic!(
                "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                return_code, termination_reason
            );
        }
    }

    let reboot_controller =
        fuchsia_component::client::connect_to_service::<MockRebootControllerMarker>().unwrap();

    (sampler_app, test_component, reboot_controller)
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
#[fuchsia::test]
async fn sampler_inspect_test() {
    let (mut sampler_app, _test_component, _reboot_controller) = setup().await;

    let _sampler_app: fasync::Task<Result<(), Error>> = fasync::Task::spawn(async move {
        sampler_app.wait().await.unwrap();
        panic!("we crash the reboot server, so sampler_app should never exit.")
    });

    // Observe verification shows up in inspect.
    let hierarchy = sampler_inspect_hierarchy().await;

    // TODO(42067): Introduce better fencing so we can
    // guarantee we fetch the hierachy after the metrics were sampled
    // AND fully processed.
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
                    "metrics_configured": 5 as u64,
                    "cobalt_logs_sent": AnyProperty,
                }
            },
            "fuchsia.inspect.Health": {
                "start_timestamp_nanos": AnyProperty,
                "status": AnyProperty
            }
        }
    );
}
