// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::lowpan_context::LowpanContext;
use anyhow::Error;
use fidl_fuchsia_lowpan_device::{DeviceExtraProxy, DeviceProxy};
use fidl_fuchsia_lowpan_test::DeviceTestProxy;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};

/// Perform Wpan FIDL operations.
///
/// Note this object is shared among all threads created by server.
#[derive(Debug)]
pub struct WpanFacade {
    /// The proxy to access the lowpan Device service.
    device: RwLock<Option<DeviceProxy>>,
    /// The proxy to access the lowpan DeviceTest service.
    device_test: RwLock<Option<DeviceTestProxy>>,
    /// The proxy to access the lowpan DeviceExtra service.
    device_extra: RwLock<Option<DeviceExtraProxy>>,
}

impl WpanFacade {
    pub fn new() -> WpanFacade {
        WpanFacade {
            device: RwLock::new(None),
            device_test: RwLock::new(None),
            device_extra: RwLock::new(None),
        }
    }

    /// Returns the DeviceTestManager proxy provided on instantiation
    /// or establishes a new connection.
    pub async fn initialize_proxies(&self) -> Result<(), Error> {
        let (device, device_extra, device_test) = match LowpanContext::new(None) {
            Ok(low_pan_context) => low_pan_context.get_default_device_proxies().await?,
            _ => bail!("Error retrieving default device proxies"),
        };
        let device_rw = self.device.upgradable_read();
        let device_extra_rw = self.device_extra.upgradable_read();
        let device_test_rw = self.device_test.upgradable_read();
        *RwLockUpgradableReadGuard::upgrade(device_rw) = Some(device.clone());
        *RwLockUpgradableReadGuard::upgrade(device_extra_rw) = Some(device_extra.clone());
        *RwLockUpgradableReadGuard::upgrade(device_test_rw) = Some(device_test.clone());
        Ok(())
    }

    /// Returns the thread rloc from the DeviceTest proxy service.
    pub async fn get_thread_rloc16(&self) -> Result<u16, Error> {
        let thread_rloc16 = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_thread_rloc16().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(thread_rloc16)
    }

    /// Returns the current mac address (thread random mac address) from the DeviceTest
    /// proxy service.
    pub async fn get_ncp_mac_address(&self) -> Result<Vec<u8>, Error> {
        let current_mac_address = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_current_mac_address().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(current_mac_address)
    }

    /// Returns the ncp channel from the DeviceTest proxy service.
    pub async fn get_ncp_channel(&self) -> Result<u16, Error> {
        let current_channel = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_current_channel().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(current_channel)
    }

    /// Returns the current rssi from the DeviceTest proxy service.
    pub async fn get_ncp_rssi(&self) -> Result<i32, Error> {
        let ncp_rssi = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_current_rssi().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(ncp_rssi)
    }

    ///Returns the factory mac address from the DeviceTest proxy service.
    pub async fn get_weave_node_id(&self) -> Result<Vec<u8>, Error> {
        let factory_mac_address = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_factory_mac_address().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(factory_mac_address)
    }

    ///Returns the network name from the DeviceExtra proxy service.
    pub async fn get_network_name(&self) -> Result<Vec<u8>, Error> {
        let raw_name = match self.device_extra.read().as_ref() {
            Some(device_extra) => device_extra.watch_identity().await?.raw_name,
            _ => bail!("DeviceExtra proxy is not set, please call initialize_proxies first"),
        };
        match raw_name {
            Some(raw_name) => Ok(raw_name),
            None => bail!("Network name is not specified!"),
        }
    }

    ///Returns the partition id from the DeviceTest proxy service.
    pub async fn get_partition_id(&self) -> Result<u32, Error> {
        let partition_id = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_partition_id().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(partition_id)
    }

    ///Returns the thread router id from the DeviceTest proxy service.
    pub async fn get_thread_router_id(&self) -> Result<u8, Error> {
        let router_id = match self.device_test.read().as_ref() {
            Some(device_test) => device_test.get_thread_router_id().await?,
            _ => bail!("DeviceTest proxy is not set, please call initialize_proxies first"),
        };
        Ok(router_id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::ServiceMarker;
    use fidl_fuchsia_lowpan_device::{DeviceExtraMarker, DeviceMarker};
    use fidl_fuchsia_lowpan_test::DeviceTestMarker;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use lazy_static::lazy_static;
    use lowpan_driver_common::DummyDevice;
    use lowpan_driver_common::ServeTo;

    lazy_static! {
        static ref MOCK_TESTER: MockTester = MockTester::new();
    }

    struct MockTester {
        dummy_device: DummyDevice,
    }
    impl MockTester {
        fn new() -> Self {
            Self { dummy_device: DummyDevice::default() }
        }

        fn create_endpoints<T: ServiceMarker>() -> (RwLock<Option<T::Proxy>>, T::RequestStream) {
            let (client_ep, server_ep) = fidl::endpoints::create_endpoints::<T>().unwrap();
            (RwLock::new(Some(client_ep.into_proxy().unwrap())), server_ep.into_stream().unwrap())
        }

        pub fn create_facade_and_serve(
            &'static self,
        ) -> (
            WpanFacade,
            (
                impl Future<Output = anyhow::Result<()>>,
                impl Future<Output = anyhow::Result<()>>,
                impl Future<Output = anyhow::Result<()>>,
            ),
        ) {
            let (device_proxy, device_server) = MockTester::create_endpoints::<DeviceMarker>();
            let (device_test_proxy, device_test_server) =
                MockTester::create_endpoints::<DeviceTestMarker>();
            let (device_extra_proxy, device_extra_server) =
                MockTester::create_endpoints::<DeviceExtraMarker>();

            let facade = WpanFacade {
                device: device_proxy,
                device_test: device_test_proxy,
                device_extra: device_extra_proxy,
            };

            (
                facade,
                (
                    self.dummy_device.serve_to(device_server),
                    self.dummy_device.serve_to(device_test_server),
                    self.dummy_device.serve_to(device_extra_server),
                ),
            )
        }

        pub async fn assert_wpan_fn<TResult>(
            func: impl Future<Output = Result<TResult, Error>>,
            server_future: (
                impl Future<Output = anyhow::Result<()>>,
                impl Future<Output = anyhow::Result<()>>,
                impl Future<Output = anyhow::Result<()>>,
            ),
        ) {
            let facade_fut = async move {
                let awaiting = func.await;
                awaiting.expect("No value returned!");
            };
            futures::select! {
                err = server_future.0.fuse() => panic!("Server task stopped: {:?}", err),
                err = server_future.1.fuse() => panic!("Server task stopped: {:?}", err),
                err = server_future.2.fuse() => panic!("Server task stopped: {:?}", err),
                _ = facade_fut.fuse() => (),
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_thread_rloc16() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_thread_rloc16(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_ncp_channel() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_ncp_channel(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_ncp_mac_address() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_ncp_mac_address(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_ncp_rssi() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_ncp_rssi(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_weave_node_id() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_weave_node_id(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_network_name() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_network_name(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_partition_id() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_partition_id(), facade.1).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_thread_router_id() {
        let facade = MOCK_TESTER.create_facade_and_serve();
        MockTester::assert_wpan_fn(facade.0.get_thread_router_id(), facade.1).await;
    }
}
