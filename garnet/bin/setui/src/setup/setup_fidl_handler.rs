// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_processor::{process_stream, RequestContext};
use crate::switchboard::base::{
    ConfigurationInterfaceFlags, SettingRequest, SettingResponse, SettingResponseResult,
    SettingType, SetupInfo, Switchboard,
};
use crate::switchboard::hanging_get_handler::Sender;
use fidl_fuchsia_settings::{
    SetupMarker, SetupRequest, SetupRequestStream, SetupSetResponder, SetupSettings,
    SetupWatchResponder,
};
use fuchsia_async as fasync;
use futures::future::LocalBoxFuture;
use futures::FutureExt;
use parking_lot::RwLock;
use std::convert::TryFrom;
use std::sync::Arc;

impl Sender<SetupSettings> for SetupWatchResponder {
    fn send_response(self, data: SetupSettings) {
        self.send(data).unwrap();
    }
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

fn reboot(
    switchboard_handle: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    responder: SetupSetResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    let mut switchboard = switchboard_handle.write();

    if switchboard.request(SettingType::Power, SettingRequest::Reboot, response_tx).is_err() {
        // Respond immediately with an error if request was not possible.
        responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
        return;
    }

    fasync::spawn(async move {
        // Return success if we get a Ok result from the
        // switchboard.
        if let Ok(Ok(_)) = response_rx.await {
            responder.send(&mut Ok(())).ok();
            return;
        }

        responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
    });
}

fn set(
    context: RequestContext<SetupSettings, SetupWatchResponder>,
    settings: SetupSettings,
    responder: SetupSetResponder,
) {
    if let Ok(request) = SettingRequest::try_from(settings) {
        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<SettingResponseResult>();

        let mut switchboard = context.switchboard.write();

        if switchboard.request(SettingType::Setup, request, response_tx).is_err() {
            // Respond immediately with an error if request was not possible.
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            return;
        }

        let switchboard_clone = context.switchboard.clone();
        fasync::spawn(async move {
            // Return success if we get a Ok result from the
            // switchboard.
            if let Ok(Ok(_)) = response_rx.await {
                reboot(switchboard_clone, responder);
                return;
            }

            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
        });
    }
}

pub fn spawn_setup_fidl_handler(
    switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    stream: SetupRequestStream,
) {
    process_stream::<SetupMarker, SetupSettings, SetupWatchResponder>(
        stream,
        switchboard,
        SettingType::Setup,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<SetupRequest>, failure::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        SetupRequest::Set { settings, responder } => {
                            set(context, settings, responder);
                        }
                        SetupRequest::Watch { responder } => {
                            context.watch(responder).await;
                        }
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            },
        ),
    );
}
