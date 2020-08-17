// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    light::types::{SerializableGroupInfo, SerializableInfo, SerializableRgb},
};
use anyhow::Error;
use fidl_fuchsia_hardware_light::{LightMarker, LightProxy, Rgb};
use fuchsia_syslog::macros::*;
use glob::glob;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};

/// Perform Light operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct LightFacade {
    proxy: RwLock<Option<LightProxy>>,
}

impl LightFacade {
    pub fn new() -> Self {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: LightProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self) -> Result<LightProxy, Error> {
        let tag = "LightFacade::get_proxy";

        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<LightMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get light proxy {:?}", e)
                ),
            };

            let found_path = glob("/dev/class/light/*")?.filter_map(|entry| entry.ok()).next();
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

    pub async fn get_num_lights(&self) -> Result<u32, Error> {
        Ok(self.get_proxy()?.get_num_lights().await?)
    }

    pub async fn get_num_light_groups(&self) -> Result<u32, Error> {
        Ok(self.get_proxy()?.get_num_light_groups().await?)
    }

    pub async fn get_info(&self, index: u32) -> Result<SerializableInfo, Error> {
        let tag = "LightFacade::get_info";
        match self.get_proxy()?.get_info(index).await? {
            Ok(r) => Ok(SerializableInfo::new(&r)),
            Err(e) => fx_err_and_bail!(&with_line!(tag), format_err!("GetInfo failed {:?}", e)),
        }
    }

    pub async fn get_current_simple_value(&self, index: u32) -> Result<bool, Error> {
        let tag = "LightFacade::get_current_simple_value";
        match self.get_proxy()?.get_current_simple_value(index).await? {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetCurrentSimpleValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_simple_value(&self, index: u32, value: bool) -> Result<(), Error> {
        let tag = "LightFacade::set_simple_value";
        match self.get_proxy()?.set_simple_value(index, value).await? {
            Ok(r) => Ok(r),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("SetSimpleValue failed {:?}", e))
            }
        }
    }

    pub async fn get_current_brightness_value(&self, index: u32) -> Result<f64, Error> {
        let tag = "LightFacade::get_current_brightness_value";
        match self.get_proxy()?.get_current_brightness_value(index).await? {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetCurrentBrightnessValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_brightness_value(&self, index: u32, value: f64) -> Result<(), Error> {
        let tag = "LightFacade::set_brightness_value";
        match self.get_proxy()?.set_brightness_value(index, value).await? {
            Ok(r) => Ok(r),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("SetBrightnessValue failed {:?}", e))
            }
        }
    }

    pub async fn get_current_rgb_value(&self, index: u32) -> Result<SerializableRgb, Error> {
        let tag = "LightFacade::get_current_rgb_value";
        match self.get_proxy()?.get_current_rgb_value(index).await? {
            Ok(r) => Ok(SerializableRgb::new(&r)),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("GetCurrentRgbValue failed {:?}", e))
            }
        }
    }

    pub async fn set_rgb_value(&self, index: u32, value: SerializableRgb) -> Result<(), Error> {
        let tag = "LightFacade::set_rgb_value";
        let mut rgb = Rgb { red: value.red, green: value.green, blue: value.blue };
        match self.get_proxy()?.set_rgb_value(index, &mut rgb).await? {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(&with_line!(tag), format_err!("SetRgbValue failed {:?}", e)),
        }
    }

    pub async fn get_group_info(&self, index: u32) -> Result<SerializableGroupInfo, Error> {
        let tag = "LightFacade::get_group_info";
        match self.get_proxy()?.get_group_info(index).await? {
            Ok(r) => Ok(SerializableGroupInfo::new(&r)),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("GetGroupInfo failed {:?}", e))
            }
        }
    }

    pub async fn get_group_current_simple_value(&self, index: u32) -> Result<Vec<bool>, Error> {
        let tag = "LightFacade::get_group_current_simple_value";
        match self.get_proxy()?.get_group_current_simple_value(index).await? {
            Ok(r) => match r {
                Some(v) => Ok(v),
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Expected a vector")),
            },
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetGroupCurrentSimpleValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_group_simple_value(&self, index: u32, value: Vec<bool>) -> Result<(), Error> {
        let tag = "LightFacade::set_group_simple_value";
        match self.get_proxy()?.set_group_simple_value(index, &mut value.into_iter()).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("SetGroupSimpleValue failed {:?}", e)
            ),
        }
    }

    pub async fn get_group_current_brightness_value(&self, index: u32) -> Result<Vec<f64>, Error> {
        let tag = "LightFacade::get_group_current_brightness_value";
        match self.get_proxy()?.get_group_current_brightness_value(index).await? {
            Ok(r) => match r {
                Some(v) => Ok(v),
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Expected a vector")),
            },
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetGroupCurrentBrightnessValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_group_brightness_value(
        &self,
        index: u32,
        value: Vec<f64>,
    ) -> Result<(), Error> {
        let tag = "LightFacade::set_group_brightness_value";
        match self.get_proxy()?.set_group_brightness_value(index, &value).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("SetGroupBrightnessValue failed {:?}", e)
            ),
        }
    }

    pub async fn get_group_current_rgb_value(
        &self,
        index: u32,
    ) -> Result<Vec<SerializableRgb>, Error> {
        let tag = "LightFacade::get_group_current_rgb_value";
        let values = match self.get_proxy()?.get_group_current_rgb_value(index).await? {
            Ok(r) => match r {
                Some(v) => v,
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Expected a vector")),
            },
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetGroupCurrentRgbValue failed {:?}", e)
            ),
        };
        let ret = values
            .into_iter()
            .map(|x| SerializableRgb { red: x.red, green: x.green, blue: x.blue })
            .collect::<Vec<_>>();
        Ok(ret)
    }

    pub async fn set_group_rgb_value(
        &self,
        index: u32,
        value: Vec<SerializableRgb>,
    ) -> Result<(), Error> {
        let tag = "LightFacade::set_group_rgb_value";
        let mut rgb = value
            .into_iter()
            .map(|x| Rgb { red: x.red, green: x.green, blue: x.blue })
            .collect::<Vec<Rgb>>();
        match self.get_proxy()?.set_group_rgb_value(index, &mut rgb.iter_mut()).await? {
            Ok(_) => Ok(()),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("SetGroupRgbValue failed {:?}", e))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::light::types::SerializableCapability;
    use fidl_fuchsia_hardware_light::{Capability, GroupInfo, Info, LightError, LightRequest, Rgb};
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;

    struct MockLightBuilder {
        expected: Vec<Box<dyn FnOnce(LightRequest) + Send + 'static>>,
    }

    impl MockLightBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(LightRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_get_num_lights(self, count: u32) -> Self {
            self.push(move |req| match req {
                LightRequest::GetNumLights { responder } => responder.send(count).unwrap(),
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_num_light_groups(self, count: u32) -> Self {
            self.push(move |req| match req {
                LightRequest::GetNumLightGroups { responder } => responder.send(count).unwrap(),
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_info(self, idx: u32, res: Result<SerializableInfo, LightError>) -> Self {
            self.push(move |req| match req {
                LightRequest::GetInfo { index, responder } => {
                    assert_eq!(idx, index);
                    responder
                        .send(&mut res.map(|info| Info {
                            name: info.name.clone(),
                            capability: Capability::from(info.capability),
                        }))
                        .unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_current_simple_value(self, idx: u32, res: Result<bool, LightError>) -> Self {
            self.push(move |req| match req {
                LightRequest::GetCurrentSimpleValue { index, responder } => {
                    assert_eq!(idx, index);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_simple_value(self, idx: u32, val: bool, res: Result<(), LightError>) -> Self {
            self.push(move |req| match req {
                LightRequest::SetSimpleValue { index, value, responder } => {
                    assert_eq!(idx, index);
                    assert_eq!(val, value);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_current_brightness_value(
            self,
            idx: u32,
            res: Result<f64, LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::GetCurrentBrightnessValue { index, responder } => {
                    assert_eq!(idx, index);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_brightness_value(
            self,
            idx: u32,
            val: f64,
            res: Result<(), LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::SetBrightnessValue { index, value, responder } => {
                    assert_eq!(idx, index);
                    assert_eq!(val, value);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_current_rgb_value(
            self,
            idx: u32,
            res: Result<SerializableRgb, LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::GetCurrentRgbValue { index, responder } => {
                    assert_eq!(idx, index);
                    responder
                        .send(&mut res.map(|rgb| Rgb {
                            red: rgb.red,
                            green: rgb.green,
                            blue: rgb.blue,
                        }))
                        .unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_rgb_value(
            self,
            idx: u32,
            val: SerializableRgb,
            res: Result<(), LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::SetRgbValue { index, value, responder } => {
                    assert_eq!(idx, index);
                    assert_eq!(val, SerializableRgb::new(&value));
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_group_info(
            self,
            id: u32,
            res: Result<SerializableGroupInfo, LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::GetGroupInfo { group_id, responder } => {
                    assert_eq!(id, group_id);
                    responder
                        .send(&mut res.map(|info| GroupInfo {
                            name: info.name.clone(),
                            count: info.count,
                            capability: Capability::from(info.capability),
                        }))
                        .unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_group_current_simple_value(
            self,
            id: u32,
            res: Result<Vec<bool>, LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::GetGroupCurrentSimpleValue { group_id, responder } => {
                    assert_eq!(id, group_id);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_group_simple_value(
            self,
            id: u32,
            vals: Vec<bool>,
            res: Result<(), LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::SetGroupSimpleValue { group_id, values, responder } => {
                    assert_eq!(id, group_id);
                    assert_eq!(vals, values);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_group_current_brightness_value(
            self,
            id: u32,
            res: Result<Vec<f64>, LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::GetGroupCurrentBrightnessValue { group_id, responder } => {
                    assert_eq!(id, group_id);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_group_brightness_value(
            self,
            id: u32,
            vals: Vec<f64>,
            res: Result<(), LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::SetGroupBrightnessValue { group_id, values, responder } => {
                    assert_eq!(id, group_id);
                    assert_eq!(vals, values);
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_group_current_rgb_value(
            self,
            id: u32,
            res: Result<Option<Vec<SerializableRgb>>, LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::GetGroupCurrentRgbValue { group_id, responder } => {
                    assert_eq!(id, group_id);
                    responder
                        .send(&mut res.map(|opt| {
                            opt.map(|vec| {
                                vec.iter().map(|x| Into::into(x.clone())).collect::<Vec<_>>()
                            })
                        }))
                        .unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_set_group_rgb_value(
            self,
            id: u32,
            vals: Vec<SerializableRgb>,
            res: Result<(), LightError>,
        ) -> Self {
            self.push(move |req| match req {
                LightRequest::SetGroupRgbValue { group_id, values, responder } => {
                    assert_eq!(id, group_id);
                    assert_eq!(vals, values.iter().map(|x| Into::into(*x)).collect::<Vec<_>>());
                    responder.send(&mut res.map(Into::into)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (LightFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<LightMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (LightFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_num_lights_ok() {
        let (facade, lights) = MockLightBuilder::new().expect_get_num_lights(3).build();
        let test = async move {
            assert_matches!(facade.get_num_lights().await, Ok(3));
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_num_light_groups_ok() {
        let (facade, lights) = MockLightBuilder::new().expect_get_num_light_groups(5).build();
        let test = async move {
            assert_matches!(facade.get_num_light_groups().await, Ok(5));
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_info_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_get_info(
                4,
                Ok(SerializableInfo {
                    name: "test_light".to_string(),
                    capability: SerializableCapability::Rgb,
                }),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.get_info(4).await,
                Ok(info) if info == SerializableInfo {
                    name: "test_light".to_string(),
                    capability: SerializableCapability::Rgb
                }
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_current_simple_value_ok() {
        let (facade, lights) =
            MockLightBuilder::new().expect_get_current_simple_value(2, Ok(true)).build();
        let test = async move {
            assert_matches!(facade.get_current_simple_value(2).await, Ok(true));
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_simple_value_ok() {
        let (facade, lights) =
            MockLightBuilder::new().expect_set_simple_value(1, false, Ok(())).build();
        let test = async move {
            assert_matches!(facade.set_simple_value(1, false).await, Ok(()));
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_current_brightness_value_ok() {
        let (facade, lights) =
            MockLightBuilder::new().expect_get_current_brightness_value(0, Ok(0.3321)).build();
        let test = async move {
            assert_matches!(facade.get_current_brightness_value(0).await, Ok(v) if v == 0.3321);
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_brightness_value_ok() {
        let (facade, lights) =
            MockLightBuilder::new().expect_set_brightness_value(1, 0.2515, Ok(())).build();
        let test = async move {
            assert_matches!(facade.set_brightness_value(1, 0.2515).await, Ok(()));
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_current_rgb_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_get_current_rgb_value(4, Ok(SerializableRgb { red: 21, green: 94, blue: 59 }))
            .build();
        let test = async move {
            assert_matches!(
                facade.get_current_rgb_value(4).await,
                Ok(SerializableRgb { red: 21, green: 94, blue: 59 })
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_rgb_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_set_rgb_value(3, SerializableRgb { red: 33, green: 45, blue: 155 }, Ok(()))
            .build();
        let test = async move {
            assert_matches!(
                facade.set_rgb_value(3, SerializableRgb { red: 33, green: 45, blue: 155 }).await,
                Ok(())
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_group_info_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_get_group_info(
                2,
                Ok(SerializableGroupInfo {
                    name: "test_light_group".to_string(),
                    count: 5,
                    capability: SerializableCapability::Brightness,
                }),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.get_group_info(2).await,
                Ok(info) if info == SerializableGroupInfo {
                    name: "test_light_group".to_string(),
                    count: 5,
                    capability: SerializableCapability::Brightness
                }
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_group_current_simple_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_get_group_current_simple_value(0, Ok(vec![false, true, false]))
            .build();
        let test = async move {
            assert_matches!(
                facade.get_group_current_simple_value(0).await,
                Ok(v) if v == vec![false, true, false]
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_group_simple_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_set_group_simple_value(3, vec![true, true, false], Ok(()))
            .build();
        let test = async move {
            assert_matches!(
                facade.set_group_simple_value(3, vec![true, true, false]).await,
                Ok(())
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_group_current_brightness_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_get_group_current_brightness_value(1, Ok(vec![0.46, 0.2, 0.77, 0.315, 0.8]))
            .build();
        let test = async move {
            assert_matches!(
                facade.get_group_current_brightness_value(1).await,
                Ok(v) if v == vec![0.46, 0.2, 0.77, 0.315, 0.8]
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_group_brightness_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_set_group_brightness_value(2, vec![0.0, 0.96, 0.37, 0.63], Ok(()))
            .build();
        let test = async move {
            assert_matches!(
                facade.set_group_brightness_value(2, vec![0.0, 0.96, 0.37, 0.63]).await,
                Ok(())
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_group_current_rgb_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_get_group_current_rgb_value(
                0,
                Ok(Some(vec![
                    SerializableRgb { red: 24, green: 41, blue: 121 },
                    SerializableRgb { red: 83, green: 23, blue: 155 },
                ])),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.get_group_current_rgb_value(0).await,
                Ok(v) if v == vec![
                    SerializableRgb { red: 24, green: 41, blue: 121 },
                    SerializableRgb { red: 83, green: 23, blue: 155 }
                ]
            );
        };

        join!(lights, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn set_group_rgb_value_ok() {
        let (facade, lights) = MockLightBuilder::new()
            .expect_set_group_rgb_value(
                1,
                vec![
                    SerializableRgb { red: 0, green: 3, blue: 3 },
                    SerializableRgb { red: 111, green: 222, blue: 11 },
                    SerializableRgb { red: 194, green: 9, blue: 70 },
                ],
                Ok(()),
            )
            .build();
        let test = async move {
            assert_matches!(
                facade
                    .set_group_rgb_value(
                        1,
                        vec![
                            SerializableRgb { red: 0, green: 3, blue: 3 },
                            SerializableRgb { red: 111, green: 222, blue: 11 },
                            SerializableRgb { red: 194, green: 9, blue: 70 }
                        ]
                    )
                    .await,
                Ok(())
            );
        };

        join!(lights, test);
    }
}
