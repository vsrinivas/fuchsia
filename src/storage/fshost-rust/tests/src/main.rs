// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_events::{
        events::{Event, EventMode, EventSource, EventSubscription, Stopped},
        matcher::EventMatcher,
    },
    fidl_fuchsia_boot as fboot, fidl_fuchsia_fshost as fshost, fidl_fuchsia_io as fio,
    fidl_fuchsia_logger as flogger,
    fuchsia_component_test::new::{
        Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
    },
    futures::FutureExt,
};

mod mocks;

#[cfg(feature = "fshost_cpp")]
const FSHOST_URL: &'static str = "#meta/test-fshost.cm";
#[cfg(feature = "fshost_rust")]
const FSHOST_URL: &'static str = "#meta/test-fshost-rust.cm";

async fn new_realm() -> Result<RealmInstance, Error> {
    let mocks = mocks::new_mocks().await;
    let builder = RealmBuilder::new().await?;
    println!("using {} as fshost", FSHOST_URL);
    let fshost = builder.add_child("fshost", FSHOST_URL, ChildOptions::new().eager()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fshost::AdminMarker>())
                .from(&fshost)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<flogger::LogSinkMarker>())
                .from(Ref::parent())
                .to(&fshost),
        )
        .await?;
    let mocks =
        builder.add_local_child("mocks", move |h| mocks(h).boxed(), ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fboot::ArgumentsMarker>())
                .capability(Capability::protocol::<fboot::ItemsMarker>())
                .capability(Capability::directory("dev").path("/dev").rights(fio::RW_STAR_DIR))
                .from(&mocks)
                .to(&fshost),
        )
        .await?;
    Ok(builder.build().await?)
}

#[fuchsia::test]
async fn admin_shutdown_shuts_down_fshost() {
    let realm = new_realm().await.unwrap();

    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    let admin = realm.root.connect_to_protocol_at_exposed_dir::<fshost::AdminMarker>().unwrap();
    admin.shutdown().await.unwrap();

    EventMatcher::ok()
        .moniker_regex("realm_builder:.*/fshost")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();

    realm.destroy().await.unwrap();
}
