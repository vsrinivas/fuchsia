// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::find_file;
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::gpio::types::SerializableGpioFlags;
use anyhow::Error;
use fidl_fuchsia_hardware_gpio::{GpioMarker, GpioProxy};
use fuchsia_syslog::macros::*;
use parking_lot::RwLock;
use std::path::Path;

/// Perform Gpio operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct GpioFacade {
    proxy: RwLock<Option<GpioProxy>>,
}

impl GpioFacade {
    pub fn new() -> GpioFacade {
        GpioFacade { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: GpioProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self, pin: u32) -> Result<GpioProxy, Error> {
        let tag = "GpioFacade::get_proxy";

        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<GpioMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get gpio proxy {:?}", e)
                ),
            };

            let find_path = format!("gpio-{}", pin);
            let found_path = match find_file(Path::new("/dev/sys/platform"), &find_path) {
                Ok(p) => p,
                Err(_) => fx_err_and_bail!(&with_line!(tag), format_err!("Failed to find path")),
            };

            fdio::service_connect(found_path.to_str().unwrap(), server.into_channel())?;
            Ok(proxy)
        }
    }

    pub async fn config_in(&self, pin: u32, flags: SerializableGpioFlags) -> Result<(), Error> {
        let tag = "gpiofacade::config_in";
        match self.get_proxy(pin)?.config_in(Into::into(flags)).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("could not config gpio pin in: {:?}", e)
            ),
        }
    }

    pub async fn config_out(&self, pin: u32, value: u8) -> Result<(), Error> {
        let tag = "gpiofacade::config_out";
        match self.get_proxy(pin)?.config_out(value).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("could not config gpio pin out: {:?}", e)
            ),
        }
    }

    pub async fn read(&self, pin: u32) -> Result<u8, Error> {
        let tag = "gpiofacade::read";
        match self.get_proxy(pin)?.read().await? {
            Ok(val) => Ok(val),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("could not read gpio pin: {:?}", e))
            }
        }
    }

    pub async fn write(&self, pin: u32, value: u8) -> Result<(), Error> {
        let tag = "gpiofacade::write";
        match self.get_proxy(pin)?.write(value).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("could not write to gpio pin: {:?}", e)
            ),
        }
    }

    pub async fn set_drive_strength(&self, pin: u32, ds_ua: u64) -> Result<u64, Error> {
        let tag = "gpiofacade::set_drive_strength";
        match self.get_proxy(pin)?.set_drive_strength(ds_ua).await? {
            Ok(ds) => Ok(ds),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("could not set drive strength of gpio pin: {:?}", e)
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_gpio::GpioRequest;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;

    struct MockGpioBuilder {
        expected: Vec<Box<dyn FnOnce(GpioRequest) + Send + 'static>>,
    }

    impl MockGpioBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(GpioRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_config_in(
            self,
            _pin: u32,
            flag: SerializableGpioFlags,
            res: Result<(), i32>,
        ) -> Self {
            self.push(move |req| match req {
                GpioRequest::ConfigIn { flags, responder } => {
                    assert_eq!(flag, Into::into(flags));
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_config_out(self, _pin: u32, val: u8, res: Result<(), i32>) -> Self {
            self.push(move |req| match req {
                GpioRequest::ConfigOut { initial_value, responder } => {
                    assert_eq!(val, initial_value);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_read(self, _pin: u32, res: Result<u8, i32>) -> Self {
            self.push(move |req| match req {
                GpioRequest::Read { responder } => {
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_write(self, _pin: u32, val: u8, res: Result<(), i32>) -> Self {
            self.push(move |req| match req {
                GpioRequest::Write { value, responder } => {
                    assert_eq!(val, value);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_drive_strength(self, _pin: u32, ds: u64, res: Result<u64, i32>) -> Self {
            self.push(move |req| match req {
                GpioRequest::SetDriveStrength { ds_ua, responder } => {
                    assert_eq!(ds, ds_ua);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (GpioFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<GpioMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (GpioFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_in_ok() {
        let (facade, gpio) = MockGpioBuilder::new()
            .expect_config_in(0, SerializableGpioFlags::PullUp, Ok(()))
            .build();
        let test = async move {
            assert_matches!(facade.config_in(0, SerializableGpioFlags::PullUp).await, Ok(_));
        };

        join!(gpio, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_out_ok() {
        let (facade, gpio) = MockGpioBuilder::new().expect_config_out(0, 8, Ok(())).build();
        let test = async move {
            assert_matches!(facade.config_out(0, 8).await, Ok(_));
        };

        join!(gpio, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_ok() {
        let (facade, gpio) = MockGpioBuilder::new().expect_read(0, Ok(9)).build();
        let test = async move {
            assert_matches!(
                facade.read(0).await,
                Ok(v) if v == 9
            );
        };

        join!(gpio, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_ok() {
        let (facade, gpio) = MockGpioBuilder::new().expect_write(0, 15, Ok(())).build();
        let test = async move {
            assert_matches!(facade.write(0, 15).await, Ok(_));
        };

        join!(gpio, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_drive_strength_ok() {
        let (facade, gpio) =
            MockGpioBuilder::new().expect_set_drive_strength(0, 2000, Ok(2000)).build();
        let test = async move {
            assert_matches!(facade.set_drive_strength(0, 2000).await, Ok(ds) if ds == 2000);
        };

        join!(gpio, test);
    }
}
