// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl_fuchsia_sysinfo::{SysInfoMarker, SysInfoProxy};
use fuchsia_syslog::macros::*;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};

/// Perform SysInfo operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct SysInfoFacade {
    proxy: RwLock<Option<SysInfoProxy>>,
}

impl SysInfoFacade {
    pub fn new() -> SysInfoFacade {
        SysInfoFacade { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: SysInfoProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self) -> Result<SysInfoProxy, Error> {
        let tag = "SysInfoFacade::get_proxy";

        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<SysInfoMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get SysInfo proxy {:?}", e)
                ),
            };

            fdio::service_connect("/svc/fuchsia.sysinfo.SysInfo", server.into_channel())?;
            *RwLockUpgradableReadGuard::upgrade(lock) = Some(proxy.clone());
            Ok(proxy)
        }
    }

    pub async fn get_board_name(&self) -> Result<String, Error> {
        let tag = "SysInfoFacade::get_board_name";
        let (error, name) = self.get_proxy()?.get_board_name().await?;
        match error {
            0 => Ok(name.unwrap_or("Unable to get name".to_string())),
            _ => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get board name {:?}", error)
            ),
        }
    }

    pub async fn get_board_revision(&self) -> Result<u32, Error> {
        let tag = "SysInfoFacade::get_board_revision";
        let (error, revision) = self.get_proxy()?.get_board_revision().await?;
        match error {
            0 => Ok(revision),
            _ => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get board revision {:?}", error)
            ),
        }
    }

    pub async fn get_bootloader_vendor(&self) -> Result<String, Error> {
        let tag = "SysInfoFacade::get_bootloader_vendor";
        let (error, vendor) = self.get_proxy()?.get_bootloader_vendor().await?;
        match error {
            0 => Ok(vendor.unwrap_or("Unable to get bootloader vendor".to_string())),
            _ => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get bootloader vendor {:?}", error)
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_sysinfo::SysInfoRequest;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;

    struct MockSysInfoBuilder {
        expected: Vec<Box<dyn FnOnce(SysInfoRequest) + Send + 'static>>,
    }

    impl MockSysInfoBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(SysInfoRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_get_board_name(self, status: i32, name: Option<&'static str>) -> Self {
            self.push(move |req| match req {
                SysInfoRequest::GetBoardName { responder } => responder.send(status, name).unwrap(),
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_board_revision(self, status: i32, revision: u32) -> Self {
            self.push(move |req| match req {
                SysInfoRequest::GetBoardRevision { responder } => {
                    responder.send(status, revision).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_bootloader_vendor(self, status: i32, vendor: Option<&'static str>) -> Self {
            self.push(move |req| match req {
                SysInfoRequest::GetBootloaderVendor { responder } => {
                    responder.send(status, vendor).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (SysInfoFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<SysInfoMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (SysInfoFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_board_name_ok() {
        let (facade, sysinfo) =
            MockSysInfoBuilder::new().expect_get_board_name(0, Some("test_board")).build();
        let test = async move {
            assert_matches!(
                facade.get_board_name().await,
                Ok(s) if s == "test_board"
            );
        };

        join!(sysinfo, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_board_revision_ok() {
        let (facade, sysinfo) = MockSysInfoBuilder::new().expect_get_board_revision(0, 19).build();
        let test = async move {
            assert_matches!(
                facade.get_board_revision().await,
                Ok(r) if r == 19
            );
        };

        join!(sysinfo, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_bootloader_vendor_ok() {
        let (facade, sysinfo) =
            MockSysInfoBuilder::new().expect_get_bootloader_vendor(0, Some("test_vendor")).build();
        let test = async move {
            assert_matches!(
                facade.get_bootloader_vendor().await,
                Ok(v) if v == "test_vendor"
            );
        };

        join!(sysinfo, test);
    }
}
