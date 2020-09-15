use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    cpu_ctrl::types::SerializableCpuPerformanceStateInfo,
};
use anyhow::Error;
use fidl_fuchsia_hardware_cpu_ctrl::{DeviceMarker, DeviceProxy};
use fuchsia_syslog::macros::*;
use glob::glob;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};

/// Perform cpu-ctrl operations.
///
/// Note this object is shared among all threads created
///

#[derive(Debug)]
pub struct CpuCtrlFacade {
    proxy: RwLock<Option<DeviceProxy>>,
}

impl CpuCtrlFacade {
    pub fn new() -> CpuCtrlFacade {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: DeviceProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self, device_number: String) -> Result<DeviceProxy, Error> {
        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let tag = "CpuCtrlFacade::get_proxy";
            let (proxy, server) = match fidl::endpoints::create_proxy::<DeviceMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get cpu-ctrl proxy {:?}", e)
                ),
            };
            let mut path = String::from("/dev/class/cpu-ctrl/");
            path.push_str(&device_number);
            let found_path = glob(&path)?.filter_map(|entry| entry.ok()).next();
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

    pub async fn get_performance_state_info(
        &self,
        device_number: String,
        state: u32,
    ) -> Result<SerializableCpuPerformanceStateInfo, Error> {
        let tag = "CpuCtrlFacade::get_performance_state_info";
        match self.get_proxy(device_number)?.get_performance_state_info(state).await? {
            Ok(r) => Ok(SerializableCpuPerformanceStateInfo::from(r)),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetPerformanceStateInfo failed: {:?}", e)
            ),
        }
    }

    pub async fn get_num_logical_cores(&self, device_number: String) -> Result<u64, Error> {
        Ok(self.get_proxy(device_number)?.get_num_logical_cores().await?)
    }

    pub async fn get_logical_core_id(
        &self,
        device_number: String,
        index: u64,
    ) -> Result<u64, Error> {
        Ok(self.get_proxy(device_number)?.get_logical_core_id(index).await?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_cpu_ctrl::{CpuPerformanceStateInfo, DeviceRequest};
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;

    struct MockCpuCtrlBuilder {
        expected: Vec<Box<dyn FnOnce(DeviceRequest) + Send + 'static>>,
    }

    impl MockCpuCtrlBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(DeviceRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_get_performance_state_info(
            self,
            _device_number: String,
            expected_state: u32,
            res: Result<CpuPerformanceStateInfo, Error>,
        ) -> Self {
            self.push(move |req| match req {
                DeviceRequest::GetPerformanceStateInfo { state, responder } => {
                    assert_eq!(expected_state, state);
                    responder
                        .send(
                            &mut res
                                .map(|info| CpuPerformanceStateInfo {
                                    frequency_hz: info.frequency_hz,
                                    voltage_uv: info.voltage_uv,
                                })
                                .map_err(|_status| -1),
                        )
                        .unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_num_logical_cores(
            self,
            _device_number: String,
            res: Result<u64, Error>,
        ) -> Self {
            self.push(move |req| match req {
                DeviceRequest::GetNumLogicalCores { responder } => {
                    assert!(res.is_ok());
                    responder.send(res.unwrap()).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_logical_core_id(
            self,
            _device_number: String,
            expected_index: u64,
            res: Result<u64, Error>,
        ) -> Self {
            self.push(move |req| match req {
                DeviceRequest::GetLogicalCoreId { index, responder } => {
                    assert_eq!(expected_index, index);
                    assert!(res.is_ok());
                    responder.send(res.unwrap()).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (CpuCtrlFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<DeviceMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (CpuCtrlFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_performance_state_info_ok() {
        let (facade, expectations) = MockCpuCtrlBuilder::new()
            .expect_get_performance_state_info(
                "000".to_string(),
                0,
                Ok(CpuPerformanceStateInfo { frequency_hz: 1896000000, voltage_uv: 981000 }),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.get_performance_state_info("000".to_string(), 0).await,
                Ok(info) if info == SerializableCpuPerformanceStateInfo {
                    frequency_hz: 1896000000,
                    voltage_uv: 981000,
                }
            );
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_num_logical_cores_ok() {
        let (facade, expectations) = MockCpuCtrlBuilder::new()
            .expect_get_num_logical_cores("000".to_string(), Ok(4))
            .build();
        let test = async move {
            assert_matches!(facade.get_num_logical_cores("000".to_string()).await, Ok(4));
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_logical_core_id_ok() {
        let (facade, expectations) = MockCpuCtrlBuilder::new()
            .expect_get_logical_core_id("000".to_string(), 0, Ok(0))
            .build();
        let test = async move {
            assert_matches!(facade.get_logical_core_id("000".to_string(), 0).await, Ok(0));
        };

        join!(expectations, test);
    }
}
