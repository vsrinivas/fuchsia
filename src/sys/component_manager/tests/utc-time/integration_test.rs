// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use component_events::{
    events::*,
    matcher::*,
    sequence::{EventSequence, Ordering},
};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use log::*;
use test_utils_lib::opaque_test::OpaqueTestBuilder;
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, file::vmo::read_only_static,
    pseudo_directory,
};

// This value must be kept consistent with the value in maintainer.rs
const EXPECTED_BACKSTOP_TIME_SEC_STR: &str = "1589910459";

#[fasync::run_singlethreaded(test)]
async fn builtin_time_service_and_clock_routed() {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::WARN);

    // Construct a pseudo-directory to mock the component manager's configured
    // backstop time.
    let dir = pseudo_directory! {
        "config" => pseudo_directory! {
            "build_info" => pseudo_directory! {
                // The backstop time is stored in seconds.
                "minimum_utc_stamp" => read_only_static(EXPECTED_BACKSTOP_TIME_SEC_STR),
            },
        },
    };

    let (client, server) = zx::Channel::create().expect("failed to create channel pair");
    let scope = ExecutionScope::new();
    dir.open(
        scope,
        fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server),
    );

    // Start a component_manager as a v1 component, with the extra `--maintain-utc-clock` flag.
    debug!("starting component_manager");
    let test = OpaqueTestBuilder::new("fuchsia-pkg://fuchsia.com/utc-time-tests#meta/realm.cm")
        .component_manager_url(
            "fuchsia-pkg://fuchsia.com/utc-time-tests#meta/component_manager.cmx",
        )
        .config("/pkg/data/cm_config")
        .add_dir_handle("/boot", client.into())
        .build()
        .await
        .expect("failed to start the OpaqueTest");

    let event_source = test
        .connect_to_event_source()
        .await
        .expect("failed to connect to the EventSource protocol");

    let event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    // Unblock the component_manager.
    debug!("starting component tree");
    event_source.start_component_tree().await;

    // Wait for both components to exit cleanly.
    // The child components do several assertions on UTC time properties.
    // If any assertion fails, the component will fail with non-zero exit code.
    EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok().stop(Some(ExitStatusMatcher::Clean)).moniker("./time_client:0"),
                EventMatcher::ok().stop(Some(ExitStatusMatcher::Clean)).moniker("./maintainer:0"),
            ],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
