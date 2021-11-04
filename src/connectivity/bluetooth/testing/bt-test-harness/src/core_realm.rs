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
    fidl_fuchsia_driver_test as fdt, fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_stash::SecureStoreMarker,
    fuchsia_component_test::{
        ChildProperties, RealmBuilder, RealmInstance, RouteBuilder, RouteEndpoint, ScopedInstance,
    },
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    futures::FutureExt,
    realmbuilder_mock_helpers::stateless_mock_responder,
};

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
pub struct CoreRealm {
    realm: RealmInstance,
}

impl CoreRealm {
    pub async fn create() -> Result<Self, Error> {
        let builder = RealmBuilder::new().await?;
        let _ = builder.driver_test_realm_setup().await?;
        let _ = builder
            .add_child(constants::bt_init::MONIKER, constants::bt_init::URL, ChildProperties::new())
            .await?
            // Required by bt-init/bt-gap.
            .add_child(
                constants::secure_stash::MONIKER,
                constants::secure_stash::URL,
                ChildProperties::new(),
            )
            .await?
            // Mock components for dependencies of bt-init/bt-gap to silence warnings about
            // missing dependencies.
            .add_mock_child(
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
                ChildProperties::new(),
            )
            .await?
            .add_mock_child(
                constants::mock_snoop::MONIKER,
                |handles| {
                    stateless_mock_responder::<SnoopMarker, _>(handles, |req| {
                        let (_, _, responder) =
                            req.into_start().ok_or(format_err!("got unexpected SnoopRequest"))?;
                        Ok(responder.send(&mut fbt::Status { error: None })?)
                    })
                    .boxed()
                },
                ChildProperties::new(),
            )
            .await?
            // Route required capabilities from AboveRoot to realm components.
            .add_route(
                RouteBuilder::protocol_marker::<LogSinkMarker>()
                    .source(RouteEndpoint::AboveRoot)
                    .targets(vec![
                        RouteEndpoint::component(constants::bt_init::MONIKER),
                        RouteEndpoint::component(constants::secure_stash::MONIKER),
                    ]),
            )
            .await?
            .add_route(
                RouteBuilder::storage("tmp", "/data")
                    .source(RouteEndpoint::AboveRoot)
                    .targets(vec![RouteEndpoint::component(constants::secure_stash::MONIKER)]),
            )
            .await?
            // Route bt-init/bt-gap requirements to bt-init.
            .add_route(
                RouteBuilder::protocol_marker::<SecureStoreMarker>()
                    .source(RouteEndpoint::component(constants::secure_stash::MONIKER))
                    .targets(vec![RouteEndpoint::component(constants::bt_init::MONIKER)]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<NameProviderMarker>()
                    .source(RouteEndpoint::component(constants::mock_name_provider::MONIKER))
                    .targets(vec![RouteEndpoint::component(constants::bt_init::MONIKER)]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<SnoopMarker>()
                    .source(RouteEndpoint::component(constants::mock_snoop::MONIKER))
                    .targets(vec![RouteEndpoint::component(constants::bt_init::MONIKER)]),
            )
            .await?
            // TODO(fxbug.dev/78757): Route /dev/class/bt-host to bt-init upon RealmBuilder support.
            .add_route(
                RouteBuilder::directory("dev", "/dev", fio2::RW_STAR_DIR)
                    .source(RouteEndpoint::component(fuchsia_driver_test::COMPONENT_NAME))
                    .targets(vec![RouteEndpoint::component(constants::bt_init::MONIKER)]),
            )
            .await?
            // Route capabilities used by test code AboveRoot.
            .add_route(
                RouteBuilder::protocol_marker::<fbgatt::Server_Marker>()
                    .source(RouteEndpoint::component(constants::bt_init::MONIKER))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<fble::CentralMarker>()
                    .source(RouteEndpoint::component(constants::bt_init::MONIKER))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<fble::PeripheralMarker>()
                    .source(RouteEndpoint::component(constants::bt_init::MONIKER))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<fbsys::AccessMarker>()
                    .source(RouteEndpoint::component(constants::bt_init::MONIKER))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<fbsys::HostWatcherMarker>()
                    .source(RouteEndpoint::component(constants::bt_init::MONIKER))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?
            .add_route(
                RouteBuilder::protocol_marker::<fbredr::ProfileMarker>()
                    .source(RouteEndpoint::component(constants::bt_init::MONIKER))
                    .targets(vec![RouteEndpoint::AboveRoot]),
            )
            .await?;
        let instance = builder.build().await?;

        // Start DriverTestRealm
        let args = fdt::RealmArgs {
            root_driver: Some("fuchsia-boot:///#driver/platform-bus.so".to_string()),
            ..fdt::RealmArgs::EMPTY
        };
        instance.driver_test_realm_start(args).await?;
        Ok(Self { realm: instance })
    }

    pub fn instance(&self) -> &ScopedInstance {
        &self.realm.root
    }
}
