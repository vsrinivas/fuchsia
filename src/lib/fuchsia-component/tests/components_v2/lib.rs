// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync, fuchsia_syslog as syslog,
    log::*,
    test_utils_lib::{
        events::{Event, Stopped},
        matcher::EventMatcher,
        opaque_test::OpaqueTest,
        sequence::{EventSequence, Ordering},
    },
};

#[fasync::run_singlethreaded(test)]
async fn scoped_instances() -> Result<(), Error> {
    syslog::init_with_tags(&["fuchsia_component_v2_test"]).expect("could not initialize logging");
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/fuchsia-component-tests#meta/realm.cm")
            .await?;

    let mut event_source = test.connect_to_event_source().await?;
    let event = EventMatcher::ok().r#type(Stopped::TYPE).moniker("./coll:auto-*".to_string());
    let expected_events: Vec<_> = (0..3).map(|_| event.clone()).collect();
    let expectation = EventSequence::new()
        .all_of(expected_events, Ordering::Ordered)
        .subscribe_and_expect(&mut event_source)
        .await?;

    event_source.start_component_tree().await;
    info!("Waiting for scoped instances to be destroyed");
    expectation.await?;
    Ok(())
}
