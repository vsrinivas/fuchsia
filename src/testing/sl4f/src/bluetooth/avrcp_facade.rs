// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::types::{CustomAvcPanelCommand, CustomPlayStatus};
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_bluetooth_avrcp::{
    ControllerMarker, ControllerProxy, PeerManagerMarker, PeerManagerProxy,
};
use fuchsia_component::client;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use parking_lot::RwLock;

/// AvrcpFacadeInner contains the proxies used by the AvrcpFacade.
#[derive(Debug)]
struct AvrcpFacadeInner {
    /// Proxy for the PeerManager service.
    avrcp_service_proxy: Option<PeerManagerProxy>,
    /// Proxy for the Controller service.
    controller_proxy: Option<ControllerProxy>,
}

#[derive(Debug)]
pub struct AvrcpFacade {
    inner: RwLock<AvrcpFacadeInner>,
}

impl AvrcpFacade {
    pub fn new() -> AvrcpFacade {
        AvrcpFacade {
            inner: RwLock::new(AvrcpFacadeInner {
                avrcp_service_proxy: None,
                controller_proxy: None,
            }),
        }
    }

    /// Creates the proxy to the PeerManager service.
    async fn create_avrcp_service_proxy(&self) -> Result<PeerManagerProxy, Error> {
        let tag = "AvrcpFacade::create_avrcp_service_proxy";
        match self.inner.read().avrcp_service_proxy.clone() {
            Some(avrcp_service_proxy) => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Current AVRCP service proxy: {:?}",
                    avrcp_service_proxy
                );
                Ok(avrcp_service_proxy)
            }
            None => {
                let avrcp_service_proxy = client::connect_to_service::<PeerManagerMarker>();
                if let Err(err) = avrcp_service_proxy {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to create AVRCP service proxy: {}", err)
                    );
                }
                avrcp_service_proxy
            }
        }
    }

    /// Initialize the AVRCP service and retrieve the controller for the provided Bluetooth target id.
    ///
    /// # Arguments
    /// * `target_id` - a string id representing the target peer
    pub async fn init_avrcp(&self, target_id: String) -> Result<(), Error> {
        let tag = "AvrcpFacade::init_avrcp";
        self.inner.write().avrcp_service_proxy = Some(self.create_avrcp_service_proxy().await?);
        let avrcp_service_proxy = match &self.inner.read().avrcp_service_proxy {
            Some(p) => p.clone(),
            None => fx_err_and_bail!(&with_line!(tag), "No AVRCP service proxy created"),
        };
        let (cont_client, cont_server) = create_endpoints::<ControllerMarker>()?;
        let _status =
            avrcp_service_proxy.get_controller_for_target(&target_id.as_str(), cont_server).await?;
        self.inner.write().controller_proxy =
            Some(cont_client.into_proxy().expect("Error obtaining controller client proxy"));
        Ok(())
    }

    /// Returns the media attributes from the controller.
    pub async fn get_media_attributes(&self) -> Result<String, Error> {
        let tag = "AvrcpFacade::get_media_attributes";
        match self.inner.read().controller_proxy.clone() {
            Some(proxy) => match proxy.get_media_attributes().await? {
                Ok(media_attribs) => Ok(format!("Media attributes: {:#?}", media_attribs)),
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format!("Error fetching media attributes: {:?}", e)
                ),
            },
            None => fx_err_and_bail!(&with_line!(tag), "No AVRCP service proxy available"),
        }
    }

    /// Returns the play status from the controller.
    pub async fn get_play_status(&self) -> Result<CustomPlayStatus, Error> {
        let tag = "AvrcpFacade::get_play_status";
        match self.inner.read().controller_proxy.clone() {
            Some(proxy) => match proxy.get_play_status().await? {
                Ok(play_status) => Ok(CustomPlayStatus::new(&play_status)),
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format!("Error fetching play status: {:?}", e)
                ),
            },
            None => fx_err_and_bail!(&with_line!(tag), "No AVRCP service proxy available"),
        }
    }

    /// Sends an AVCPanelCommand to the controller.
    ///
    /// # Arguments
    /// * `command` - an enum representing the AVCPanelCommand
    pub async fn send_command(&self, command: CustomAvcPanelCommand) -> Result<(), Error> {
        let tag = "AvrcpFacade::send_command";
        let result = match self.inner.read().controller_proxy.clone() {
            Some(proxy) => proxy.send_command(command.into()).await?,
            None => fx_err_and_bail!(&with_line!(tag), "No AVRCP service proxy available"),
        };
        match result {
            Ok(res) => Ok(res),
            Err(err) => {
                fx_err_and_bail!(&with_line!(tag), format!("Error sending command:{:?}", err))
            }
        }
    }

    /// Sends an AVCPanelCommand to the controller.
    ///
    /// # Arguments
    /// * `absolute_volume` - the value to which the volume is set
    pub async fn set_absolute_volume(&self, absolute_volume: u8) -> Result<u8, Error> {
        let tag = "AvrcpFacade::set_absolute_volume";
        let result = match self.inner.read().controller_proxy.clone() {
            Some(proxy) => proxy.set_absolute_volume(absolute_volume).await?,
            None => fx_err_and_bail!(&with_line!(tag), "No AVRCP service proxy available"),
        };
        match result {
            Ok(res) => Ok(res),
            Err(err) => {
                fx_err_and_bail!(&with_line!(tag), format!("Error setting volume:{:?}", err))
            }
        }
    }

    /// A function to remove the profile service proxy and clear connected devices.
    fn clear(&self) {
        self.inner.write().avrcp_service_proxy = None;
        self.inner.write().controller_proxy = None;
    }

    /// Cleanup any Profile Server related objects.
    pub async fn cleanup(&self) -> Result<(), Error> {
        self.clear();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::{ControllerRequest, PlayStatus};
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use futures::Future;
    use lazy_static::lazy_static;
    use matches::assert_matches;

    lazy_static! {
        static ref PLAY_STATUS: CustomPlayStatus = CustomPlayStatus {
            song_length: Some(120),
            song_position: Some(10),
            playback_status: Some(4),
        };
    }
    struct MockAvrcpTester {
        expected_state: Vec<Box<dyn FnOnce(ControllerRequest) + Send + 'static>>,
    }

    impl MockAvrcpTester {
        fn new() -> Self {
            Self { expected_state: vec![] }
        }

        fn push(mut self, request: impl FnOnce(ControllerRequest) + Send + 'static) -> Self {
            self.expected_state.push(Box::new(request));
            self
        }

        fn build_controller(self) -> (AvrcpFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) = create_proxy_and_stream::<ControllerMarker>().unwrap();
            let fut = async move {
                for expected in self.expected_state {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };
            (
                AvrcpFacade {
                    inner: RwLock::new(AvrcpFacadeInner {
                        controller_proxy: Some(proxy),
                        avrcp_service_proxy: None,
                    }),
                },
                fut,
            )
        }

        fn expect_get_play_status(self, result: CustomPlayStatus) -> Self {
            self.push(move |req| match req {
                ControllerRequest::GetPlayStatus { responder } => {
                    responder.send(&mut Ok(PlayStatus::from(result))).unwrap();
                }
                _ => {}
            })
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_play_status() {
        let (facade, play_status_fut) =
            MockAvrcpTester::new().expect_get_play_status(*PLAY_STATUS).build_controller();
        let facade_fut = async move {
            let play_status = facade.get_play_status().await.unwrap();
            assert_eq!(play_status, *PLAY_STATUS);
        };
        future::join(facade_fut, play_status_fut).await;
    }
}
