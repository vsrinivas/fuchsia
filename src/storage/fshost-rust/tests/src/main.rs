// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_events::{
        events::{Event, EventMode, EventSource, EventSubscription, Stopped},
        matcher::EventMatcher,
    },
    fidl_fuchsia_boot as fboot, fidl_fuchsia_device_manager as fdevicemanager,
    fidl_fuchsia_fshost as fshost, fidl_fuchsia_io2 as fio2, fidl_fuchsia_logger as flogger,
    fuchsia_component_test::{
        ChildOptions, RealmBuilder, RealmInstance, RouteBuilder, RouteEndpoint,
    },
    futures::{channel::mpsc, FutureExt, StreamExt},
    matches::assert_matches,
};

mod mocks;

#[cfg(feature = "fshost_cpp")]
const FSHOST_URL: &'static str = "#meta/test-fshost.cm";
#[cfg(feature = "fshost_rust")]
const FSHOST_URL: &'static str = "#meta/test-fshost-rust.cm";

async fn new_realm() -> Result<(RealmInstance, mpsc::UnboundedReceiver<mocks::Signal>), Error> {
    let (mocks, rx) = mocks::new_mocks().await;
    let builder = RealmBuilder::new().await?;
    println!("using {} as fshost", FSHOST_URL);
    builder
        .add_child("fshost", FSHOST_URL, ChildOptions::new())
        .await?
        .mark_as_eager("fshost")
        .await?
        .add_route(
            RouteBuilder::protocol_marker::<fshost::AdminMarker>()
                .source(RouteEndpoint::component("fshost"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol_marker::<flogger::LogSinkMarker>()
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("fshost")]),
        )
        .await?
        .add_mock_child("mocks", move |h| mocks(h).boxed(), ChildOptions::new())
        .await?
        .add_route(
            RouteBuilder::protocol_marker::<fboot::ArgumentsMarker>()
                .source(RouteEndpoint::component("mocks"))
                .targets(vec![RouteEndpoint::component("fshost")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol_marker::<fboot::ItemsMarker>()
                .source(RouteEndpoint::component("mocks"))
                .targets(vec![RouteEndpoint::component("fshost")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol_marker::<fdevicemanager::AdministratorMarker>()
                .source(RouteEndpoint::component("mocks"))
                .targets(vec![RouteEndpoint::component("fshost")]),
        )
        .await?
        .add_route(
            RouteBuilder::directory("dev", "/dev", fio2::RW_STAR_DIR)
                .source(RouteEndpoint::component("mocks"))
                .targets(vec![RouteEndpoint::component("fshost")]),
        )
        .await?;
    Ok((builder.build().await?, rx))
}

#[fuchsia::test]
async fn admin_shutdown_shuts_down_fshost() {
    let (realm, mut rx) = new_realm().await.unwrap();

    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    let admin = realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    admin.shutdown().await.unwrap();

    assert_matches!(rx.next().await, Some(mocks::Signal::UnregisterSystemStorageForShutdown));

    EventMatcher::ok()
        .moniker_regex("realm_builder:.*/fshost")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    realm.destroy().await.unwrap();
}
