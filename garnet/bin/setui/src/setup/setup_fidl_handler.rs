// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_processor::{process_stream, RequestContext};
use crate::switchboard::base::{
    ConfigurationInterfaceFlags, SettingRequest, SettingResponse, SettingType, SetupInfo,
    SwitchboardClient,
};
use crate::switchboard::hanging_get_handler::Sender;
use fidl_fuchsia_settings::{
    Error, SetupMarker, SetupRequest, SetupRequestStream, SetupSettings, SetupWatchResponder,
};
use futures::future::LocalBoxFuture;
use futures::FutureExt;
use std::convert::TryFrom;

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

async fn reboot(switchboard_client: SwitchboardClient) -> Result<(), Error> {
    if let Ok(response_rx) =
        switchboard_client.request(SettingType::Power, SettingRequest::Reboot).await
    {
        if let Ok(Ok(_)) = response_rx.await {
            return Ok(());
        }
    }
    return Err(fidl_fuchsia_settings::Error::Failed);
}

async fn set(
    context: RequestContext<SetupSettings, SetupWatchResponder>,
    settings: SetupSettings,
    do_reboot: bool,
) -> Result<(), Error> {
    if let Ok(request) = SettingRequest::try_from(settings) {
        if let Ok(response_rx) =
            context.switchboard_client.request(SettingType::Setup, request).await
        {
            let switchboard_client = context.switchboard_client.clone();
            if let Ok(Ok(_)) = response_rx.await {
                if do_reboot {
                    return Ok(());
                }
                return reboot(switchboard_client).await;
            }
        }
    }
    return Err(fidl_fuchsia_settings::Error::Failed);
}

pub fn spawn_setup_fidl_handler(switchboard_client: SwitchboardClient, stream: SetupRequestStream) {
    process_stream::<SetupMarker, SetupSettings, SetupWatchResponder>(
        stream,
        switchboard_client,
        SettingType::Setup,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<SetupRequest>, anyhow::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        SetupRequest::Set { settings, responder } => {
                            match set(context, settings, true).await {
                                Ok(_) => responder.send(&mut Ok(())).ok(),
                                Err(e) => responder.send(&mut Err(e)).ok(),
                            };
                        }
                        SetupRequest::Set2 { settings, reboot_device, responder } => {
                            match set(context, settings, reboot_device).await {
                                Ok(_) => responder.send(&mut Ok(())).ok(),
                                Err(e) => responder.send(&mut Err(e)).ok(),
                            };
                        }
                        SetupRequest::Watch { responder } => {
                            context.watch(responder).await;
                        }
                        _ => return Ok(Some(req)),
                    }

                    return Ok(None);
                }
                .boxed_local()
            },
        ),
    );
}
