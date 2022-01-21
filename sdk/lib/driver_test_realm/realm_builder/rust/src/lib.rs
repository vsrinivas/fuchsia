// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    fidl_fuchsia_driver_test as fdt,
    fuchsia_component_test::new::{
        Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
    },
};

pub const COMPONENT_NAME: &str = "driver_test_realm";
pub const DRIVER_TEST_REALM_URL: &str = "#meta/driver_test_realm.cm";

#[async_trait::async_trait]
pub trait DriverTestRealmBuilder {
    /// Set up the DriverTestRealm component in the RealmBuilder realm.
    /// This configures proper input/output routing of capabilities.
    /// This takes a `manifest_url` to use, which is used by tests that need to
    /// specify a custom driver test realm.
    async fn driver_test_realm_manifest_setup(&self, manifest_url: &str) -> Result<&Self>;
    /// Set up the DriverTestRealm component in the RealmBuilder realm.
    /// This configures proper input/output routing of capabilities.
    async fn driver_test_realm_setup(&self) -> Result<&Self>;
}

#[async_trait::async_trait]
impl DriverTestRealmBuilder for RealmBuilder {
    async fn driver_test_realm_manifest_setup(&self, manifest_url: &str) -> Result<&Self> {
        let driver_realm =
            self.add_child(COMPONENT_NAME, manifest_url, ChildOptions::new().eager()).await?;
        self.add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.process.Launcher"))
                .capability(Capability::protocol_by_name("fuchsia.sys.Launcher"))
                .from(Ref::parent())
                .to(&driver_realm),
        )
        .await?;
        self.add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.driver.development.DriverDevelopment",
                ))
                .capability(Capability::protocol_by_name("fuchsia.driver.test.Realm"))
                .capability(Capability::directory("dev"))
                .from(&driver_realm)
                .to(Ref::parent()),
        )
        .await?;
        Ok(&self)
    }

    async fn driver_test_realm_setup(&self) -> Result<&Self> {
        self.driver_test_realm_manifest_setup(DRIVER_TEST_REALM_URL).await
    }
}

#[async_trait::async_trait]
pub trait DriverTestRealmInstance {
    /// Connect to the DriverTestRealm in this Instance and call Start with `args`.
    async fn driver_test_realm_start(&self, args: fdt::RealmArgs) -> Result<()>;

    /// Connect to the /dev/ directory hosted by  DriverTestRealm in this Instance.
    fn driver_test_realm_connect_to_dev(&self) -> Result<fidl_fuchsia_io::DirectoryProxy>;
}

#[async_trait::async_trait]
impl DriverTestRealmInstance for RealmInstance {
    async fn driver_test_realm_start(&self, args: fdt::RealmArgs) -> Result<()> {
        let config = self.root.connect_to_protocol_at_exposed_dir::<fdt::RealmMarker>()?;
        config
            .start(args)
            .await?
            .map_err(|e| anyhow!("DriverTestRealm Start failed with: {}", e))?;
        Ok(())
    }

    fn driver_test_realm_connect_to_dev(&self) -> Result<fidl_fuchsia_io::DirectoryProxy> {
        let (dev, dev_server) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
        self.root
            .connect_request_to_named_protocol_at_exposed_dir("dev", dev_server.into_channel())?;
        Ok(fidl_fuchsia_io::DirectoryProxy::new(fidl::handle::AsyncChannel::from_channel(
            dev.into_channel(),
        )?))
    }
}
