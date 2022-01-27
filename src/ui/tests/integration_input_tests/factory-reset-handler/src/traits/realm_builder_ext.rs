// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::traits::test_realm_component::TestRealmComponent,
    fidl::endpoints::DiscoverableProtocolMarker,
    fuchsia_component_test::new::{Capability, RealmBuilder, Ref, Route},
};

/// Ergonomic extension of RealmBuilder.
///
/// Allows callers to deal in `TestRealmComponent` instances, instead of instances
/// of &'str or String which represent `Moniker`s.
///
/// Note that the methods of RealmBuilderExt deliberately do _not_ return the
/// builder. This is because `rustfmt` usually formats long call chains in a
/// way that makes the code longer than just repeating the variable every time.
/// (If a single call in the chain wraps, `rustfmt` formats every call to expand
/// out the arguments.)
#[async_trait::async_trait]
pub(crate) trait RealmBuilderExt {
    /// Adds `component` to the realm.
    async fn add(&self, component: &(dyn TestRealmComponent + Sync));

    /// Routes `D` from the parent realm to the given `destination` component.
    async fn route_from_parent<D: DiscoverableProtocolMarker>(
        &self,
        destination: &(dyn TestRealmComponent + Sync),
    );

    /// Routes `D` from the given `source` component to the parent realm.
    async fn route_to_parent<D: DiscoverableProtocolMarker>(
        &self,
        source: &(dyn TestRealmComponent + Sync),
    );

    /// Routes `D` from the given `source` component to the given `destination`
    /// component. Both components must be part of the test realm.
    async fn route_to_peer<D: DiscoverableProtocolMarker>(
        &self,
        source: &(dyn TestRealmComponent + Sync),
        destination: &(dyn TestRealmComponent + Sync),
    );
}

#[async_trait::async_trait]
impl RealmBuilderExt for RealmBuilder {
    async fn add(&self, component: &(dyn TestRealmComponent + Sync)) {
        component.add_to_builder(&self).await;

        // Route `fuchsia.logger.LogSink` to `component`. This ensures that `component`s
        // logs go to the test log, rather than the global syslog. (The capability from
        // the parent realm is set up to connect to the test runner; without that, the
        // messages probably go to `stdout`, and then from `stdout` to the global syslog.)
        self.route_from_parent::<fidl_fuchsia_logger::LogSinkMarker>(component).await;
    }

    async fn route_from_parent<D: DiscoverableProtocolMarker>(
        &self,
        destination: &(dyn TestRealmComponent + Sync),
    ) {
        RealmBuilder::add_route(
            &self,
            Route::new()
                .capability(Capability::protocol::<D>())
                .from(Ref::parent())
                .to(destination.ref_()),
        )
        .await
        .unwrap();
    }

    async fn route_to_parent<D: DiscoverableProtocolMarker>(
        &self,
        source: &(dyn TestRealmComponent + Sync),
    ) {
        RealmBuilder::add_route(
            &self,
            Route::new()
                .capability(Capability::protocol::<D>())
                .from(source.ref_())
                .to(Ref::parent()),
        )
        .await
        .unwrap();
    }

    async fn route_to_peer<D: DiscoverableProtocolMarker>(
        &self,
        source: &(dyn TestRealmComponent + Sync),
        destination: &(dyn TestRealmComponent + Sync),
    ) {
        RealmBuilder::add_route(
            &self,
            Route::new()
                .capability(Capability::protocol::<D>())
                .from(source.ref_())
                .to(destination.ref_()),
        )
        .await
        .unwrap();
    }
}
