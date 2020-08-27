// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    ram::types::{SerializableBandwidthInfo, SerializableBandwidthMeasurementConfig},
};
use anyhow::Error;
// Auto generated fidl crate:
use fidl_fuchsia_hardware_ram_metrics::{BandwidthMeasurementConfig, DeviceMarker, DeviceProxy};
use fuchsia_syslog::macros::{fx_log_err};
use glob::glob;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};

/// Perform Ram metrics operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct RamFacade {
    proxy: RwLock<Option<DeviceProxy>>,
}

impl RamFacade {
    pub fn new() -> Self {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: DeviceProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    /// Connect to a ram device.
    /// Will connect to the first device found in the path /dev/class/aml-ram/
    /// Returns the connection if successful, otherwise returns an error.
    fn get_proxy(&self) -> Result<DeviceProxy, Error> {
        let tag = "RamFacade::get_proxy";

        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<DeviceMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get device proxy {:?}", e)
                ),
            };

            let found_path = glob("/dev/class/aml-ram/*")?.filter_map(|entry| entry.ok()).next();
            match found_path {
                Some(path) => {
                    fdio::service_connect(path.to_string_lossy().as_ref(), server.into_channel())?;
                    *RwLockUpgradableReadGuard::upgrade(lock) = Some(proxy.clone());
                    Ok(proxy)
                }
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Failed to find device")),
            }
        }
    }

    /// Call the MeasureBandwidth interface of the first available ram device.
    /// ser_config: configuration describing which channels to measure.
    /// Returns bandwidth measurement results if successful, otherwise returns error.
    /// See sdk/fidl/fuchsia.hardware.ram.metrics/metrics.fidl for more details on
    /// the input and output of this function.
    pub async fn measure_bandwidth(
        &self,
        ser_config: SerializableBandwidthMeasurementConfig,
    ) -> Result<SerializableBandwidthInfo, Error> {
        let tag = "RamFacade::measure_bandwidth";
        let mut config = BandwidthMeasurementConfig::from(ser_config);
        match self.get_proxy()?.measure_bandwidth(&mut config).await? {
            Ok(r) => Ok(SerializableBandwidthInfo::from(r)),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("MeasureBandwidth failed {:?}", e))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ram::types::{
        SerializableBandwidthInfo, SerializableBandwidthMeasurementConfig,
        SerializableGrantedCyclesResult,
    };
    use fidl_fuchsia_hardware_ram_metrics::{
        BandwidthInfo, BandwidthMeasurementConfig, DeviceRequest,
    };
    use fuchsia_zircon as zx;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;

    struct MockDeviceBuilder {
        expected: Vec<Box<dyn FnOnce(DeviceRequest) + Send + 'static>>,
    }

    impl MockDeviceBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(DeviceRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_measure_bandwidth(
            self,
            val: SerializableBandwidthMeasurementConfig,
            res: Result<SerializableBandwidthInfo, zx::zx_status_t>,
        ) -> Self {
            self.push(move |DeviceRequest::MeasureBandwidth { config, responder }| {
                assert_eq!(BandwidthMeasurementConfig::from(val), config);
                responder
                    // Here we convert the result we passed in to the fidl version
                    .send(&mut res.map(|bandwidth_info| BandwidthInfo::from(bandwidth_info)))
                    .expect("failed to respond to MeasureBandwidth request")
            })
        }
        fn build(self) -> (RamFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<DeviceMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert!(stream.next().await.is_none());
            };

            (RamFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn measure_bandwidth_ok() {
        let input_config = SerializableBandwidthMeasurementConfig {
            cycles_to_measure: 10,
            channels: [1, 2, 3, 4, 5, 6, 7, 8],
        };
        let output_info = SerializableBandwidthInfo {
            timestamp: 123_456_789,
            frequency: 5_000_000,
            bytes_per_cycle: 3_000_000,
            channels: [SerializableGrantedCyclesResult {
                read_cycles: 0,
                write_cycles: 0,
                readwrite_cycles: 0,
            }; 8],
            total: SerializableGrantedCyclesResult {
                read_cycles: 0,
                write_cycles: 0,
                readwrite_cycles: 0,
            },
        };
        let (facade, device) = MockDeviceBuilder::new()
            .expect_measure_bandwidth(
                // Change this to be the input
                input_config.clone(),
                // change this to be result with the output
                Ok(output_info.clone()),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.measure_bandwidth(input_config).await,
                Ok(info) if info == output_info);
        };

        join!(device, test);
    }
}
