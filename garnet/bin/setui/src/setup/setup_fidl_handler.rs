// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_hanging_get_responder;
use crate::fidl_process;

use crate::base::{SettingInfo, SettingType};
use crate::fidl_processor::settings::RequestContext;
use crate::handler::base::Request;
use crate::setup::types::{ConfigurationInterfaceFlags, SetupInfo};
use fidl_fuchsia_settings::{Error, SetupMarker, SetupRequest, SetupSettings, SetupWatchResponder};
use std::convert::TryFrom;

fidl_hanging_get_responder!(SetupMarker, SetupSettings, SetupWatchResponder);

impl TryFrom<SetupSettings> for Request {
    type Error = &'static str;
    fn try_from(value: SetupSettings) -> Result<Self, Self::Error> {
        if let Some(configuration_interfaces) = value.enabled_configuration_interfaces {
            return Ok(Request::SetConfigurationInterfaces(ConfigurationInterfaceFlags::from(
                configuration_interfaces,
            )));
        }

        Err("Ineligible change")
    }
}

impl From<SettingInfo> for SetupSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Setup(info) = response {
            return SetupSettings::from(info);
        }
        panic!("incorrect value sent");
    }
}

impl From<fidl_fuchsia_settings::ConfigurationInterfaces> for ConfigurationInterfaceFlags {
    fn from(interfaces: fidl_fuchsia_settings::ConfigurationInterfaces) -> Self {
        let mut flags = ConfigurationInterfaceFlags::empty();

        if interfaces.intersects(fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet) {
            flags |= ConfigurationInterfaceFlags::ETHERNET;
        }

        if interfaces.intersects(fidl_fuchsia_settings::ConfigurationInterfaces::Wifi) {
            flags |= ConfigurationInterfaceFlags::WIFI;
        }

        flags
    }
}

impl From<ConfigurationInterfaceFlags> for fidl_fuchsia_settings::ConfigurationInterfaces {
    fn from(flags: ConfigurationInterfaceFlags) -> Self {
        let mut interfaces = fidl_fuchsia_settings::ConfigurationInterfaces::empty();

        if flags.intersects(ConfigurationInterfaceFlags::ETHERNET) {
            interfaces |= fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet;
        }

        if flags.intersects(ConfigurationInterfaceFlags::WIFI) {
            interfaces |= fidl_fuchsia_settings::ConfigurationInterfaces::Wifi;
        }

        interfaces
    }
}

impl From<SetupInfo> for SetupSettings {
    fn from(info: SetupInfo) -> Self {
        let mut settings = SetupSettings::EMPTY;
        let interfaces =
            fidl_fuchsia_settings::ConfigurationInterfaces::from(info.configuration_interfaces);

        if !interfaces.is_empty() {
            settings.enabled_configuration_interfaces = Some(interfaces);
        }

        settings
    }
}

async fn reboot(context: RequestContext<SetupSettings, SetupWatchResponder>) -> Result<(), Error> {
    context
        .request(SettingType::Power, Request::Reboot)
        .await
        .map(|_| ())
        .map_err(|_| fidl_fuchsia_settings::Error::Failed)
}

async fn set(
    context: RequestContext<SetupSettings, SetupWatchResponder>,
    settings: SetupSettings,
    do_reboot: bool,
) -> Result<(), Error> {
    let request = Request::try_from(settings).map_err(|_| fidl_fuchsia_settings::Error::Failed)?;
    context
        .request(SettingType::Setup, request)
        .await
        .map_err(|_| fidl_fuchsia_settings::Error::Failed)?;
    if do_reboot {
        reboot(context).await
    } else {
        Ok(())
    }
}

async fn process_request(
    context: RequestContext<SetupSettings, SetupWatchResponder>,
    req: SetupRequest,
) -> Result<Option<SetupRequest>, anyhow::Error> {
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
            context.watch(responder, true).await;
        }
        _ => return Ok(Some(req)),
    }

    Ok(None)
}

fidl_process!(Setup, SettingType::Setup, process_request);
