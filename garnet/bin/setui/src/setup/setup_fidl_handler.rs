// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::{
    ConfigurationInterfaceFlags, SettingRequest, SettingResponse, SettingResponseResult,
    SettingType, SetupInfo, Switchboard,
};
use crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender};
use fidl_fuchsia_settings::{
    SetupRequest, SetupRequestStream, SetupSetResponder, SetupSettings, SetupWatchResponder,
};
use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::TryFutureExt;
use futures::TryStreamExt;
use std::convert::TryFrom;
use std::sync::{Arc, RwLock};

impl Sender<SetupSettings> for SetupWatchResponder {
    fn send_response(self, data: SetupSettings) {
        self.send(data).unwrap();
    }
}

pub struct SetupFidlHandler {
    switchboard_handle: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<SetupSettings, SetupWatchResponder>>>,
}

impl TryFrom<SetupSettings> for SettingRequest {
    type Error = &'static str;
    fn try_from(value: SetupSettings) -> Result<Self, Self::Error> {
        if let Some(configuration_interfaces) = value.enabled_configuration_interfaces {
            return Ok(SettingRequest::SetConfigurationInterfaces(
                ConfigurationInterfaceFlags::from(configuration_interfaces),
            ));
        }

        Err("Ineligible change")
    }
}

impl From<SettingResponse> for SetupSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Setup(info) = response {
            return SetupSettings::from(info);
        }

        panic!("incorrect value sent");
    }
}

impl From<fidl_fuchsia_settings::ConfigurationInterfaces> for ConfigurationInterfaceFlags {
    fn from(interfaces: fidl_fuchsia_settings::ConfigurationInterfaces) -> Self {
        let mut flags = ConfigurationInterfaceFlags::empty();

        if interfaces.intersects(fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet) {
            flags = flags | ConfigurationInterfaceFlags::ETHERNET;
        }

        if interfaces.intersects(fidl_fuchsia_settings::ConfigurationInterfaces::Wifi) {
            flags = flags | ConfigurationInterfaceFlags::WIFI;
        }

        return flags;
    }
}

impl From<ConfigurationInterfaceFlags> for fidl_fuchsia_settings::ConfigurationInterfaces {
    fn from(flags: ConfigurationInterfaceFlags) -> Self {
        let mut interfaces = fidl_fuchsia_settings::ConfigurationInterfaces::empty();

        if flags.intersects(ConfigurationInterfaceFlags::ETHERNET) {
            interfaces = interfaces | fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet;
        }

        if flags.intersects(ConfigurationInterfaceFlags::WIFI) {
            interfaces = interfaces | fidl_fuchsia_settings::ConfigurationInterfaces::Wifi;
        }

        return interfaces;
    }
}

impl From<SetupInfo> for SetupSettings {
    fn from(info: SetupInfo) -> Self {
        let mut settings = SetupSettings::empty();
        let interfaces =
            fidl_fuchsia_settings::ConfigurationInterfaces::from(info.configuration_interfaces);

        if !interfaces.is_empty() {
            settings.enabled_configuration_interfaces = Some(interfaces);
        }

        return settings;
    }
}

impl SetupFidlHandler {
    fn set(&self, settings: SetupSettings, responder: SetupSetResponder) {
        if let Ok(request) = SettingRequest::try_from(settings) {
            let (response_tx, response_rx) =
                futures::channel::oneshot::channel::<SettingResponseResult>();

            let mut switchboard = self.switchboard_handle.write().unwrap();

            if switchboard.request(SettingType::Setup, request, response_tx).is_err() {
                // Respond immediately with an error if request was not possible.
                responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
                return;
            }

            fasync::spawn(
                async move {
                    // Return success if we get a Ok result from the
                    // switchboard.
                    if let Ok(Ok(_)) = response_rx.await {
                        responder.send(&mut Ok(())).ok();
                        return Ok(());
                    }

                    responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
                    Ok(())
                }
                    .unwrap_or_else(|_e: failure::Error| {}),
            );
        }
    }

    async fn watch(&self, responder: SetupWatchResponder) {
        let mut hanging_get_lock = self.hanging_get_handler.lock().await;
        hanging_get_lock.watch(responder).await;
    }

    pub fn spawn(
        switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
        mut stream: SetupRequestStream,
    ) {
        fasync::spawn(async move {
            let handler = Self {
                switchboard_handle: switchboard.clone(),
                hanging_get_handler: HangingGetHandler::create(
                    switchboard.clone(),
                    SettingType::Setup,
                ),
            };

            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    SetupRequest::Set { settings, responder } => {
                        handler.set(settings, responder);
                    }
                    SetupRequest::Watch { responder } => {
                        handler.watch(responder).await;
                    }
                }
            }
        })
    }
}
