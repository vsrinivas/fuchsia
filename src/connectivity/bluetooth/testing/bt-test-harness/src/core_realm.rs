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
    fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_process,
    fidl_fuchsia_stash::SecureStoreMarker,
    fidl_fuchsia_sys,
    fuchsia_component_test::{
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
        RealmInstance, ScopedInstance,
    },
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
    pub mod isolated_devmgr {
        pub const URL: &str = "#meta/isolated-devmgr.cm";
        pub const MONIKER: &str = "isolated-devmgr";
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
        let mut builder = RealmBuilder::new().await?;
        builder
            .add_component(
                constants::bt_init::MONIKER,
                ComponentSource::url(constants::bt_init::URL),
            )
            .await?
            // Used to launch bt-emulator and bt-host devices.
            .add_component(
                constants::isolated_devmgr::MONIKER,
                ComponentSource::url(constants::isolated_devmgr::URL),
            )
            .await?
            // Required by bt-init/bt-gap.
            .add_component(
                constants::secure_stash::MONIKER,
                ComponentSource::url(constants::secure_stash::URL),
            )
            .await?
            // Mock components for dependencies of bt-init/bt-gap to silence warnings about
            // missing dependencies.
            .add_component(
                constants::mock_name_provider::MONIKER,
                ComponentSource::mock(|handles| {
                    stateless_mock_responder::<NameProviderMarker, _>(handles, |req| {
                        let responder = req
                            .into_get_device_name()
                            .ok_or(format_err!("got unexpected NameProviderRequest"))?;
                        Ok(responder.send(&mut Ok(DEFAULT_TEST_DEVICE_NAME.to_string()))?)
                    })
                    .boxed()
                }),
            )
            .await?
            .add_component(
                constants::mock_snoop::MONIKER,
                ComponentSource::mock(|handles| {
                    stateless_mock_responder::<SnoopMarker, _>(handles, |req| {
                        let (_, _, responder) =
                            req.into_start().ok_or(format_err!("got unexpected SnoopRequest"))?;
                        Ok(responder.send(&mut fbt::Status { error: None })?)
                    })
                    .boxed()
                }),
            )
            .await?;
        builder
            // Route required capabilities from AboveRoot to realm components.
            .add_protocol_route::<LogSinkMarker>(
                RouteEndpoint::AboveRoot,
                vec![
                    RouteEndpoint::component(constants::bt_init::MONIKER),
                    RouteEndpoint::component(constants::isolated_devmgr::MONIKER),
                    RouteEndpoint::component(constants::secure_stash::MONIKER),
                ],
            )?
            .add_route(CapabilityRoute {
                capability: Capability::storage("tmp", "/data"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component(constants::secure_stash::MONIKER)],
            })?
            .add_protocol_route::<fidl_fuchsia_process::LauncherMarker>(
                RouteEndpoint::AboveRoot,
                vec![RouteEndpoint::component(constants::isolated_devmgr::MONIKER)],
            )?
            .add_protocol_route::<fidl_fuchsia_sys::LauncherMarker>(
                RouteEndpoint::AboveRoot,
                vec![RouteEndpoint::component(constants::isolated_devmgr::MONIKER)],
            )?
            // Route bt-init/bt-gap requirements to bt-init.
            .add_protocol_route::<SecureStoreMarker>(
                RouteEndpoint::component(constants::secure_stash::MONIKER),
                vec![RouteEndpoint::component(constants::bt_init::MONIKER)],
            )?
            .add_protocol_route::<NameProviderMarker>(
                RouteEndpoint::component(constants::mock_name_provider::MONIKER),
                vec![RouteEndpoint::component(constants::bt_init::MONIKER)],
            )?
            .add_protocol_route::<SnoopMarker>(
                RouteEndpoint::component(constants::mock_snoop::MONIKER),
                vec![RouteEndpoint::component(constants::bt_init::MONIKER)],
            )?
            // We also expose `/dev` AboveRoot so that test code can launch and manipulate the
            // bt-hci-emulator driver.
            // TODO(fxbug.dev/78757): Route /dev/class/bt-host to bt-init upon RealmBuilder support.
            .add_route(CapabilityRoute {
                capability: Capability::directory("dev", "/dev", fio2::RW_STAR_DIR),
                source: RouteEndpoint::component(constants::isolated_devmgr::MONIKER),
                targets: vec![
                    RouteEndpoint::component(constants::bt_init::MONIKER),
                    RouteEndpoint::AboveRoot,
                ],
            })?
            // Route capabilities used by test code AboveRoot.
            .add_protocol_route::<fbgatt::Server_Marker>(
                RouteEndpoint::component(constants::bt_init::MONIKER),
                vec![RouteEndpoint::AboveRoot],
            )?
            .add_protocol_route::<fble::CentralMarker>(
                RouteEndpoint::component(constants::bt_init::MONIKER),
                vec![RouteEndpoint::AboveRoot],
            )?
            .add_protocol_route::<fble::PeripheralMarker>(
                RouteEndpoint::component(constants::bt_init::MONIKER),
                vec![RouteEndpoint::AboveRoot],
            )?
            .add_protocol_route::<fbsys::AccessMarker>(
                RouteEndpoint::component(constants::bt_init::MONIKER),
                vec![RouteEndpoint::AboveRoot],
            )?
            .add_protocol_route::<fbsys::HostWatcherMarker>(
                RouteEndpoint::component(constants::bt_init::MONIKER),
                vec![RouteEndpoint::AboveRoot],
            )?
            .add_protocol_route::<fbredr::ProfileMarker>(
                RouteEndpoint::component(constants::bt_init::MONIKER),
                vec![RouteEndpoint::AboveRoot],
            )?;
        let instance = builder.build().create().await?;
        Ok(Self { realm: instance })
    }

    pub fn instance(&self) -> &ScopedInstance {
        &self.realm.root
    }
}
