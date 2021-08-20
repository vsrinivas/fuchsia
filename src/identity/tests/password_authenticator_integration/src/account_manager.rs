// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_account::AccountManagerMarker;
use {
    fuchsia_async as fasync,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        RealmInstance,
    },
};

struct TestEnv {
    realm_instance: RealmInstance,
}

impl TestEnv {
    async fn build() -> TestEnv {
        let mut builder = RealmBuilder::new().await.unwrap();
        builder
            .add_component("password_authenticator", ComponentSource::url("fuchsia-pkg://fuchsia.com/password-authenticator-integration-tests#meta/password-authenticator.cm")).await.unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![
                    RouteEndpoint::component("password_authenticator"),
                ],
            }).unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.identity.account.AccountManager"),
                source: RouteEndpoint::component("password_authenticator"),
                targets: vec![
                    RouteEndpoint::AboveRoot,
                ],
            }).unwrap();

        let realm_instance = builder.build().create().await.unwrap();

        TestEnv { realm_instance }
    }
}

#[fasync::run_singlethreaded(test)]
async fn get_account_ids() {
    let env = TestEnv::build().await;
    let account_manager = env
        .realm_instance
        .root
        .connect_to_protocol_at_exposed_dir::<AccountManagerMarker>()
        .expect("connect to account manager");

    let account_ids = account_manager.get_account_ids().await.expect("get account ids");
    assert_eq!(account_ids, vec![1u64]);
}
