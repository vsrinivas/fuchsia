// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_driver_test as fdt,
    fuchsia_component_test::{
        new::{RealmBuilder, RealmInstance},
        ScopedInstance,
    },
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
};

use crate::emulator::EMULATOR_ROOT_DRIVER_URL;

pub const SHARED_STATE_INDEX: &str = "BT-HOST-DRIVER-REALM";
pub const DEFAULT_TEST_DEVICE_NAME: &str = "fuchsia-bt-integration-test";

/// The HostDriverRealm is a hermetic driver realm which exposes a devfs with bt-host and bt-hci
/// emulator drivers. Clients can use the `create` method to construct the realm, and the `instance`
/// method to access the production capabilities and test interfaces exposed from realm's devfs.
pub struct HostDriverRealm {
    realm: RealmInstance,
}

impl HostDriverRealm {
    pub async fn create() -> Result<Self, Error> {
        let builder = RealmBuilder::new().await?;
        let _ = builder.driver_test_realm_setup().await?;
        let instance = builder.build().await?;

        // Start DriverTestRealm
        let args = fdt::RealmArgs {
            root_driver: Some(EMULATOR_ROOT_DRIVER_URL.to_string()),
            ..fdt::RealmArgs::EMPTY
        };
        instance.driver_test_realm_start(args).await?;
        Ok(Self { realm: instance })
    }

    pub fn instance(&self) -> &ScopedInstance {
        &self.realm.root
    }
}
