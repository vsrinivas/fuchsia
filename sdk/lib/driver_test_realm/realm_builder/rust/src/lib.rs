// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    fidl_fuchsia_driver_test as fdt, fidl_fuchsia_io2 as fio2,
    fuchsia_component_test::builder::{
        Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint,
    },
    fuchsia_component_test::RealmInstance,
};

#[async_trait::async_trait]
pub trait DriverTestRealmBuilder {
    /// Set up the DriverTestRealm component in the RealmBuilder realm.
    /// This configures proper input/output routing of capabilities.
    async fn driver_test_realm_setup(&mut self) -> Result<()>;

    /// Set up the DriverTestrealm component from a specific URL.
    async fn driver_test_realm_setup_with_url(&mut self, driver_test_realm_url: &str)
        -> Result<()>;
}

#[async_trait::async_trait]
impl DriverTestRealmBuilder for RealmBuilder {
    async fn driver_test_realm_setup(&mut self) -> Result<()> {
        self.driver_test_realm_setup_with_url("#meta/driver_test_realm.cm").await
    }

    // TODO(fxbug.dev/85884): This function only exists because relative URLs in collections in
    // RealmBuilder aren't working at the moment.
    async fn driver_test_realm_setup_with_url(
        &mut self,
        driver_test_realm_url: &str,
    ) -> Result<()> {
        self.add_component("driver_test_realm", ComponentSource::url(driver_test_realm_url))
            .await?;

        let driver_realm = RouteEndpoint::component("driver_test_realm");
        self.add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![driver_realm.clone()],
        })?;
        self.add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.process.Launcher"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![driver_realm.clone()],
        })?;
        self.add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys.Launcher"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![driver_realm.clone()],
        })?;
        self.add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.driver.development.DriverDevelopment"),
            source: driver_realm.clone(),
            targets: vec![RouteEndpoint::AboveRoot],
        })?;
        self.add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.driver.test.Realm"),
            source: driver_realm.clone(),
            targets: vec![RouteEndpoint::AboveRoot],
        })?;
        self.add_route(CapabilityRoute {
            capability: Capability::directory("dev", "dev", fio2::RW_STAR_DIR),
            source: driver_realm.clone(),
            targets: vec![RouteEndpoint::AboveRoot],
        })?;
        Ok(())
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
