// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth as fbt, fidl_fuchsia_bluetooth_bredr as fbredr,
    fidl_fuchsia_bluetooth_gatt as fbgatt, fidl_fuchsia_bluetooth_le as fble,
    fidl_fuchsia_bluetooth_snoop::SnoopMarker,
    fidl_fuchsia_bluetooth_sys as fbsys,
    fidl_fuchsia_device::NameProviderMarker,
    fidl_fuchsia_driver_test as fdt,
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_stash::SecureStoreMarker,
    fuchsia_component_test::{
        new::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
        ScopedInstance,
    },
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    futures::FutureExt,
    realmbuilder_mock_helpers::stateless_mock_responder,
};

use crate::emulator::EMULATOR_ROOT_DRIVER_URL;

pub const SHARED_STATE_INDEX: &str = "BT-CORE-REALM";
pub const DEFAULT_TEST_DEVICE_NAME: &str = "fuchsia-bt-integration-test";

// Use relative URLs because the library `deps` on all of these components, so any
// components that depend (even transitively) on CoreRealm will include these components in
// their package.
mod constants {
    pub mod bt_init {
        pub const URL: &str = "#meta/test-bt-init.cm";
        pub const MONIKER: &str = "bt-init";
    }
    pub mod secure_stash {
        pub const URL: &str = "#meta/test-stash-secure.cm";
        pub const MONIKER: &str = "secure-stash";
    }
    pub mod mock_name_provider {
        pub const MONIKER: &str = "mock-name-provider";
    }
    pub mod mock_snoop {
        pub const MONIKER: &str = "mock-snoop";
    }
}

/// The CoreRealm represents a hermetic, fully-functional instance of the Fuchsia Bluetooth core
/// stack, complete with all components (bt-init, bt-gap, bt-rfcomm), the bt-host driver responsible
/// for the majority of the Bluetooth Host Subsystem, and a bt-hci emulator. Clients should use the
/// `create` method to construct an instance, and the `instance` method to access the various
/// production capabilities and test interfaces (e.g. from the bt-hci emulator) exposed from the
/// core stack. Clients of the CoreRealm must route the `tmp` storage capability from the test
/// manager to the "#realm_builder" underlying the RealmInstance.
pub struct CoreRealm {
    realm: RealmInstance,
}

impl CoreRealm {
    pub async fn create() -> Result<Self, Error> {
        let builder = RealmBuilder::new().await?;
        let _ = builder.driver_test_realm_setup().await?;
        let bt_init = builder
            .add_child(constants::bt_init::MONIKER, constants::bt_init::URL, ChildOptions::new())
            .await?;
        let secure_stash = builder
            .add_child(
                constants::secure_stash::MONIKER,
                constants::secure_stash::URL,
                ChildOptions::new(),
            )
            .await?;
        let mock_name_provider = builder
            .add_local_child(
                constants::mock_name_provider::MONIKER,
                |handles| {
                    stateless_mock_responder::<NameProviderMarker, _>(handles, |req| {
                        let responder = req
                            .into_get_device_name()
                            .ok_or(format_err!("got unexpected NameProviderRequest"))?;
                        Ok(responder.send(&mut Ok(DEFAULT_TEST_DEVICE_NAME.to_string()))?)
                    })
                    .boxed()
                },
                ChildOptions::new(),
            )
            .await?;
        let mock_snoop = builder
            .add_local_child(
                constants::mock_snoop::MONIKER,
                |handles| {
                    stateless_mock_responder::<SnoopMarker, _>(handles, |req| {
                        let (_, _, responder) =
                            req.into_start().ok_or(format_err!("got unexpected SnoopRequest"))?;
                        Ok(responder.send(&mut fbt::Status { error: None })?)
                    })
                    .boxed()
                },
                ChildOptions::new(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<LogSinkMarker>())
                    .from(Ref::parent())
                    .to(&bt_init)
                    .to(&secure_stash),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::storage("tmp"))
                    .from(Ref::parent())
                    .to(&secure_stash),
            )
            .await?;
        // Route bt-init/bt-gap requirements to bt-init.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<SecureStoreMarker>())
                    .from(&secure_stash)
                    .to(&bt_init),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<NameProviderMarker>())
                    .from(&mock_name_provider)
                    .to(&bt_init),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<SnoopMarker>())
                    .from(&mock_snoop)
                    .to(&bt_init),
            )
            .await?;

        // TODO(fxbug.dev/78757): Route /dev/class/bt-host to bt-init upon RealmBuilder support.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("dev"))
                    .from(Ref::child(fuchsia_driver_test::COMPONENT_NAME))
                    .to(&bt_init),
            )
            .await?;

        // Route capabilities used by test code AboveRoot.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fbgatt::Server_Marker>())
                    .capability(Capability::protocol::<fble::CentralMarker>())
                    .capability(Capability::protocol::<fble::PeripheralMarker>())
                    .capability(Capability::protocol::<fbsys::AccessMarker>())
                    .capability(Capability::protocol::<fbsys::HostWatcherMarker>())
                    .capability(Capability::protocol::<fbredr::ProfileMarker>())
                    .capability(Capability::protocol::<fbsys::BootstrapMarker>())
                    .from(&bt_init)
                    .to(Ref::parent()),
            )
            .await?;

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
