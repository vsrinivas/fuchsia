// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    async_trait::async_trait,
    component_events::{
        events::{Destroyed, Event},
        injectors::{CapabilityInjector as _, ProtocolInjector},
        matcher::EventMatcher,
        sequence::{EventSequence, Ordering},
    },
    fidl_fuchsia_component_client_test::{EmptyProtocolMarker, EmptyProtocolRequestStream},
    fuchsia_async as fasync, fuchsia_syslog as syslog,
    futures::{SinkExt as _, StreamExt as _},
    log::info,
    std::sync::Arc,
    test_case::test_case,
    test_utils_lib::opaque_test::OpaqueTest,
};

struct PassthroughInjector {
    sender: futures::channel::mpsc::Sender<EmptyProtocolRequestStream>,
}

#[async_trait]
impl ProtocolInjector for PassthroughInjector {
    type Marker = EmptyProtocolMarker;

    async fn serve(
        self: Arc<Self>,
        request_stream: EmptyProtocolRequestStream,
    ) -> Result<(), Error> {
        self.sender.clone().send(request_stream).await.context("failed to send request stream")
    }
}

#[test_case("fuchsia-pkg://fuchsia.com/fuchsia-component-tests#meta/realm_with_wait.cm"; "wait")]
#[test_case("fuchsia-pkg://fuchsia.com/fuchsia-component-tests#meta/realm.cm"; "no_wait")]
#[fasync::run_singlethreaded(test)]
async fn scoped_instances(root_component: &'static str) -> Result<(), Error> {
    syslog::init_with_tags(&["fuchsia_component_v2_test"]).expect("could not initialize logging");
    let test = OpaqueTest::default(root_component).await?;

    let mut event_source = test.connect_to_event_source().await?;
    let event = EventMatcher::ok().r#type(Destroyed::TYPE).moniker("./coll:auto-*".to_string());
    let mut expected_events: Vec<_> = (0..3).map(|_| event.clone()).collect();
    expected_events
        .push(EventMatcher::ok().r#type(Destroyed::TYPE).moniker("./coll:static_name".to_string()));
    let expectation = EventSequence::new()
        .all_of(expected_events, Ordering::Unordered)
        .subscribe_and_expect(&mut event_source)
        .await?;

    let (sender, mut receiver) = futures::channel::mpsc::channel(1);
    let injector = Arc::new(PassthroughInjector { sender });
    let () = injector.inject(&event_source, EventMatcher::ok()).await;

    let () = event_source.start_component_tree().await;

    // The realm component will connect to our injected capability once it's done with its work.
    // Wait for that to happen before matching on event expectations.
    info!("Waiting for test component to signal ready");
    let _request_stream = receiver.next().await.context("failed to observe capability request")?;

    info!("Waiting for scoped instances to be destroyed");
    let () = expectation.await?;

    // TODO(https://fxbug.dev/73644): prove that dropping the OpaqueTest instance causes the
    // intercepted request stream to close. That was removed from this file due to flakes. See bug
    // for details.

    Ok(())
}
