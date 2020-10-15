// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    i2c::types::TransferRequest,
};
use anyhow::Error;
use fidl_fuchsia_hardware_i2c::{Device2Marker, Device2Proxy};
use fuchsia_syslog::macros::*;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};
use std::path::Path;

use serde_json::Value;

/// Perform I2c operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct I2cFacade {
    proxy: RwLock<Option<Device2Proxy>>,
}

impl I2cFacade {
    pub fn new() -> Self {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: Device2Proxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self, device_idx: u32) -> Result<Device2Proxy, Error> {
        let tag = "I2cFacade::get_proxy";

        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<Device2Marker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get i2c proxy {:?}", e)
                ),
            };

            let device_path = format!("/dev/class/i2c/{:03}", device_idx);
            if Path::new(&device_path).exists() {
                fdio::service_connect(device_path.as_ref(), server.into_channel())?;
                *RwLockUpgradableReadGuard::upgrade(lock) = Some(proxy.clone());
                Ok(proxy)
            } else {
                fx_err_and_bail!(&with_line!(tag), format_err!("Failed to find device"));
            }
        }
    }

    pub async fn transfer(&self, args: Value) -> Result<Vec<Vec<u8>>, Error> {
        let req: TransferRequest = serde_json::from_value(args)?;
        let tag = "I2cFacade::transfer";
        match self
            .get_proxy(req.device_idx)?
            .transfer(
                &mut req.segments_is_write.into_iter(),
                &mut req.write_segments_data.iter().map(AsRef::as_ref).into_iter(),
                &req.read_segments_length,
            )
            .await?
        {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(&with_line!(tag), format_err!("Transfer failed {:?}", e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_i2c::Device2Request;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;
    use serde_json::json;

    struct MockDevice2Builder {
        expected: Vec<Box<dyn FnOnce(Device2Request) + Send + 'static>>,
    }

    impl MockDevice2Builder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(Device2Request) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_transfer(
            self,
            _device_idx: u32,
            is_write: Vec<bool>,
            write_data: Vec<Vec<u8>>,
            read_lengths: Vec<u8>,
            res: Result<Vec<Vec<u8>>, i32>,
        ) -> Self {
            self.push(move |req| match req {
                Device2Request::Transfer {
                    segments_is_write,
                    write_segments_data,
                    read_segments_length,
                    responder,
                } => {
                    assert_eq!(is_write, segments_is_write);
                    assert_eq!(write_data, write_segments_data);
                    assert_eq!(read_lengths, read_segments_length);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
            })
        }

        fn build(self) -> (I2cFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<Device2Marker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (I2cFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn transfer() {
        let (facade, device2) = MockDevice2Builder::new()
            .expect_transfer(
                0,
                vec![true, true, true, false], /* Write, then read from 0xAA*/
                vec![vec![0xAA], vec![0x0F], vec![0xAA]],
                vec![1],
                Ok(vec![vec![0x0F]]),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.transfer(
                    json!({
                        "device_idx": 0,
                        "segments_is_write" : [true, true, true, false],
                        "write_segments_data" : [[0xAA], [0x0F], [0xAA]],
                        "read_segments_length":[1]}))
                .await,
                Ok(v) if v == vec![vec![0x0F]]
            );
        };

        join!(device2, test);
    }
}
