// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::traits::test_realm_component::TestRealmComponent,
    fidl::endpoints::DiscoverableProtocolMarker,
    fuchsia_component_test::{builder::RealmBuilder, RouteEndpoint},
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
    async fn add(&mut self, component: &(dyn TestRealmComponent + Sync));

    /// Routes `D` from the parent realm to the given `destination` component.
    fn route_from_parent<D: DiscoverableProtocolMarker>(
        &mut self,
        destination: &(dyn TestRealmComponent + Sync),
    );

    /// Routes `D` from the given `source` component to the parent realm.
    fn route_to_parent<D: DiscoverableProtocolMarker>(
        &mut self,
        source: &(dyn TestRealmComponent + Sync),
    );

    /// Routes `D` from the given `source` component to the given `destination`
    /// component. Both components must be part of the test realm.
    fn route_to_peer<D: DiscoverableProtocolMarker>(
        &mut self,
        source: &(dyn TestRealmComponent + Sync),
        destination: &(dyn TestRealmComponent + Sync),
    );
}

#[async_trait::async_trait]
impl RealmBuilderExt for RealmBuilder {
    async fn add(&mut self, component: &(dyn TestRealmComponent + Sync)) {
        RealmBuilder::add_component(self, component.moniker().clone(), component.source())
            .await
            .unwrap();

        // Route `fuchsia.logger.LogSink` to `component`. This ensures that `component`s
        // logs go to the test log, rather than the global syslog. (The capability from
        // the parent realm is set up to connect to the test runner; without that, the
        // messages probably go to `stdout`, and then from `stdout` to the global syslog.)
        self.route_from_parent::<fidl_fuchsia_logger::LogSinkMarker>(component);
    }

    fn route_from_parent<D: DiscoverableProtocolMarker>(
        &mut self,
        destination: &(dyn TestRealmComponent + Sync),
    ) {
        RealmBuilder::add_protocol_route::<D>(
            self,
            RouteEndpoint::above_root(),
            vec![RouteEndpoint::component(destination.moniker().to_string())],
        )
        .unwrap();
    }

    fn route_to_parent<D: DiscoverableProtocolMarker>(
        &mut self,
        source: &(dyn TestRealmComponent + Sync),
    ) {
        RealmBuilder::add_protocol_route::<D>(
            self,
            RouteEndpoint::component(source.moniker().to_string()),
            vec![RouteEndpoint::above_root()],
        )
        .unwrap();
    }

    fn route_to_peer<D: DiscoverableProtocolMarker>(
        &mut self,
        source: &(dyn TestRealmComponent + Sync),
        destination: &(dyn TestRealmComponent + Sync),
    ) {
        RealmBuilder::add_protocol_route::<D>(
            self,
            RouteEndpoint::component(source.moniker().to_string()),
            vec![RouteEndpoint::component(destination.moniker().to_string())],
        )
        .unwrap();
    }
}
