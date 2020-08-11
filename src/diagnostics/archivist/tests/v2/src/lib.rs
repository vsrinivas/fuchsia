// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cs::log_stats::{LogSeverity, LogStats},
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
    fuchsia_inspect_contrib::reader::{ArchiveReader, Inspect},
    log::info,
    selectors,
};

const TEST_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/stub_inspect_component.cm";

// Verifies that archivist attributes logs from this component.
async fn verify_component_attributed(url: &str, expected_info_count: u64) {
    let mut response = ArchiveReader::new()
        .add_selector(format!(
            "archivist:root/log_stats/by_component/{}:*",
            selectors::sanitize_string_for_selectors(&url)
        ))
        .add_selector("archivist:root/event_stats/recent_events/*:*")
        .snapshot::<Inspect>()
        .await
        .unwrap();
    let hierarchy = response.pop().and_then(|r| r.payload).unwrap();
    let log_stats = LogStats::new_with_root(LogSeverity::INFO, &hierarchy).await.unwrap();
    let component_log_stats = log_stats.get_by_url(url).unwrap();
    let info_log_count = component_log_stats.get_count(LogSeverity::INFO);

    assert_eq!(expected_info_count, info_log_count);
}

#[fasync::run_singlethreaded(test)]
async fn read_v2_components_inspect() {
    let _test_app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT.to_string())
        .await
        .expect("Failed to create dynamic component");

    let data = ArchiveReader::new()
        .add_selector("driver/coll\\:auto-*:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_inspect_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
}

// This test verifies that Archivist knows about logging from this component.
#[fasync::run_singlethreaded(test)]
async fn log_attribution() {
    fuchsia_syslog::init().unwrap();
    info!("This is a syslog message");
    info!("This is another syslog message");
    println!("This is a debuglog message");

    verify_component_attributed(
        "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/driver.cm",
        2,
    )
    .await;
}
