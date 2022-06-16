// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use async_trait::async_trait;
use fidl_fuchsia_hardware_light::{Info, LightError, LightMarker, LightProxy};
use fuchsia_syslog::fx_log_info;

fn open_light() -> Result<LightProxy, Error> {
    eprintln!("Opening light");
    let (proxy, server) =
        fidl::endpoints::create_proxy::<LightMarker>().context("Failed to create light proxy")?;
    fdio::service_connect("/dev/class/light/000", server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}

pub struct Led {
    proxy: LightProxy,
}

impl Led {
    pub async fn new() -> Result<Led, Error> {
        fx_log_info!("Opening LEDs");
        let proxy = open_light()?;

        Ok(Led { proxy })
    }
}

#[async_trait]
pub trait LedControl: Send {
    async fn set_brightness(
        &mut self,
        index: u32,
        value: f64,
    ) -> Result<Result<(), LightError>, fidl::Error>;
    async fn get_num_lights(&mut self) -> Result<u32, fidl::Error>;
    async fn get_info(&self, index: u32) -> Result<Result<Info, LightError>, fidl::Error>;
}

#[async_trait]
impl LedControl for Led {
    async fn set_brightness(
        &mut self,
        index: u32,
        value: f64,
    ) -> Result<Result<(), LightError>, fidl::Error> {
        let value = num_traits::clamp(value, 0.0, 1.0);
        self.proxy.set_brightness_value(index, value).await
    }
    async fn get_num_lights(&mut self) -> Result<u32, fidl::Error> {
        self.proxy.get_num_lights().await
    }
    async fn get_info(&self, index: u32) -> Result<Result<Info, LightError>, fidl::Error> {
        self.proxy.get_info(index).await
    }
}

#[cfg(test)]
mod test {

    use super::LedControl;
    use super::*;
    use fidl_fuchsia_hardware_light::{Capability, LightRequest};
    use futures::{future, StreamExt};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_num_lights() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<LightMarker>().unwrap();
        let mut led = Led { proxy };

        let fut = async move {
            assert_eq!(led.get_num_lights().await.unwrap(), 4);
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(LightRequest::GetNumLights { responder }) => {
                    responder.send(4_u32).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_info_test() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<LightMarker>().unwrap();
        let led = Led { proxy };

        let fut = async move {
            let Info { name, capability } = led.get_info(7).await.unwrap().unwrap();
            assert_eq!("Fake Light", name);
            assert_eq!(Capability::Brightness, capability);
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(LightRequest::GetInfo { index, responder }) => {
                    assert_eq!(index, 7);
                    let light_info =
                        Info { name: "Fake Light".into(), capability: Capability::Brightness };
                    responder.send(&mut Ok(light_info)).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_brightness() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<LightMarker>().unwrap();
        let mut led = Led { proxy };

        let fut = async move {
            assert_eq!(led.set_brightness(3, 0.5).await.unwrap(), Ok(()));
        };

        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(LightRequest::SetBrightnessValue { index, value, responder }) => {
                    assert_eq!(index, 3);
                    assert_eq!(value, 0.5);
                    responder.send(&mut Ok(())).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(fut, stream_fut).await;
    }
}
