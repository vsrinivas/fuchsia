// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    backlight::types::SerializableState,
    common_utils::common::macros::{fx_err_and_bail, with_line},
};
use anyhow::Error;
use fidl_fuchsia_hardware_backlight::{DeviceMarker, DeviceProxy, State};
use fuchsia_syslog::macros::*;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};
use serde_json::Value;

#[derive(Debug)]
pub struct BacklightFacade {
    proxy: RwLock<Option<DeviceProxy>>,
}

impl std::convert::From<SerializableState> for State {
    fn from(state: SerializableState) -> Self {
        Self { backlight_on: state.backlight_on, brightness: state.brightness }
    }
}

impl BacklightFacade {
    pub fn new() -> BacklightFacade {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: DeviceProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self) -> Result<DeviceProxy, Error> {
        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let tag = "BacklightFacade::get_proxy";

            let (proxy, server) = match fidl::endpoints::create_proxy::<DeviceMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get backlight proxy: {:?}", e)
                ),
            };

            match fdio::service_connect("/dev/class/backlight/000", server.into_channel()) {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to connect to backlight service: {:?}", e)
                ),
            }

            *RwLockUpgradableReadGuard::upgrade(lock) = Some(proxy.clone());
            Ok(proxy)
        }
    }

    pub async fn get_state_normalized(&self, _args: Value) -> Result<SerializableState, Error> {
        match self.get_proxy()?.get_state_normalized().await? {
            Ok(r) => Ok(SerializableState::from(r)),
            Err(e) => {
                let tag = "BacklightFacade::get_state_normalized";
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("GetStateNormalized failed: {:?}", e)
                )
            }
        }
    }

    pub async fn set_state_normalized(&self, args: Value) -> Result<(), Error> {
        let state: SerializableState = serde_json::from_value(args)?;
        match self.get_proxy()?.set_state_normalized(&mut State::from(state)).await? {
            Ok(r) => Ok(r),
            Err(e) => {
                let tag = "BacklightFacade::set_state_normalized";
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("SetStateNormalized failed: {:?}", e)
                )
            }
        }
    }

    pub async fn get_normalized_brightness_scale(&self, _args: Value) -> Result<f64, Error> {
        match self.get_proxy()?.get_normalized_brightness_scale().await? {
            Ok(r) => Ok(r),
            Err(e) => {
                let tag = "BacklightFacade::get_normalized_brightness_scale";
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("GetNormalizedBrightnessScale failed: {:?}", e)
                )
            }
        }
    }

    pub async fn set_normalized_brightness_scale(&self, args: Value) -> Result<(), Error> {
        let scale: f64 = serde_json::from_value(args)?;
        match self.get_proxy()?.set_normalized_brightness_scale(scale).await? {
            Ok(r) => Ok(r),
            Err(e) => {
                let tag = "BacklightFacade::set_normalized_brightness_scale";
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("SetNormalizedBrightnessScale failed: {:?}", e)
                )
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_backlight::DeviceRequest;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;
    use serde_json::json;

    struct MockBacklightBuilder {
        expected: Vec<Box<dyn FnOnce(DeviceRequest) + Send + 'static>>,
    }

    impl MockBacklightBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(DeviceRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_get_state_normalized(self, state: State) -> Self {
            self.push(move |req| match req {
                DeviceRequest::GetStateNormalized { responder } => {
                    assert_matches!(responder.send(&mut Ok(state)), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_state_normalized(self, expected_state: State) -> Self {
            self.push(move |req| match req {
                DeviceRequest::SetStateNormalized { state, responder } => {
                    assert_eq!(state, expected_state);
                    assert_matches!(responder.send(&mut Ok(())), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_normalized_brightness_scale(self, scale: f64) -> Self {
            self.push(move |req| match req {
                DeviceRequest::GetNormalizedBrightnessScale { responder } => {
                    assert_matches!(responder.send(&mut Ok(scale)), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_normalized_brightness_scale(self, expected_scale: f64) -> Self {
            self.push(move |req| match req {
                DeviceRequest::SetNormalizedBrightnessScale { scale, responder } => {
                    assert_eq!(scale, expected_scale);
                    assert_matches!(responder.send(&mut Ok(())), Ok(()));
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (BacklightFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<DeviceMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (BacklightFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_state_normalized() {
        let (facade, expectations) = MockBacklightBuilder::new()
            .expect_get_state_normalized(State { backlight_on: true, brightness: 0.25 })
            .build();

        let test = async move {
            let result = facade.get_state_normalized(Value::Null).await;
            assert!(result.is_ok());
            assert_eq!(result.unwrap(), SerializableState { backlight_on: true, brightness: 0.25 });
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_state_normalized() {
        let (facade, expectations) = MockBacklightBuilder::new()
            .expect_set_state_normalized(State { backlight_on: false, brightness: 0.75 })
            .build();

        let test = async move {
            let result = facade
                .set_state_normalized(json!({"backlight_on": false, "brightness": 0.75}))
                .await;
            assert!(result.is_ok());
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_normalized_brightness_scale() {
        let (facade, expectations) =
            MockBacklightBuilder::new().expect_get_normalized_brightness_scale(0.75).build();

        let test = async move {
            let result = facade.get_normalized_brightness_scale(Value::Null).await;
            assert!(result.is_ok());
            assert_eq!(result.unwrap(), 0.75);
        };

        join!(expectations, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_normalized_brightness_scale() {
        let (facade, expectations) =
            MockBacklightBuilder::new().expect_set_normalized_brightness_scale(0.25).build();

        let test = async move {
            let result = facade.set_normalized_brightness_scale(json!(0.25)).await;
            assert!(result.is_ok());
        };

        join!(expectations, test);
    }
}
