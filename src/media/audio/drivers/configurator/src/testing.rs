// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
pub mod tests {
    use {
        crate::{config::Config, configurator::Configurator},
        anyhow::Error,
        async_trait::async_trait,
        device_watcher, fidl_fuchsia_io as fio,
        fuchsia_component_test::{RealmBuilder, RealmInstance},
        fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    };

    pub struct NullConfigurator {}

    #[async_trait]
    impl Configurator for NullConfigurator {
        fn new(_config: Config) -> Result<Self, Error> {
            Ok(Self {})
        }
        async fn process_new_codec(
            &mut self,
            mut _device: crate::codec::CodecInterface,
        ) -> Result<(), Error> {
            Ok(())
        }
        async fn process_new_dai(
            &mut self,
            mut _device: crate::dai::DaiInterface,
        ) -> Result<(), Error> {
            Ok(())
        }
    }

    pub async fn get_dev_proxy(
        dev_dir: &str,
    ) -> Result<(RealmInstance, fio::DirectoryProxy), Error> {
        let realm = RealmBuilder::new().await?;
        let _ = realm.driver_test_realm_setup().await?;
        let instance = realm.build().await?;
        instance.driver_test_realm_start(fidl_fuchsia_driver_test::RealmArgs::EMPTY).await?;
        let dev = instance.driver_test_realm_connect_to_dev()?;
        let dir = io_util::directory::open_directory(&dev, dev_dir, fio::OpenFlags::RIGHT_READABLE)
            .await?;
        // Wait for the first codec node 000.
        device_watcher::recursive_wait_and_open_node(&dir, "000").await?;
        Ok((instance, dir))
    }
}
