// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use crate::setup::types::{
    ConfigurationInterfaceFlags, SetConfigurationInterfacesParams, SetupInfo,
};
use fidl::prelude::*;
use fidl_fuchsia_settings::{
    SetupRequest, SetupSetResponder, SetupSetResult, SetupSettings, SetupWatchResponder,
};
use fuchsia_syslog::fx_log_warn;
use std::convert::TryFrom;

impl ErrorResponder for SetupSetResponder {
    fn id(&self) -> &'static str {
        "Setup_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
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

        if interfaces.intersects(fidl_fuchsia_settings::ConfigurationInterfaces::ETHERNET) {
            flags |= ConfigurationInterfaceFlags::ETHERNET;
        }

        if interfaces.intersects(fidl_fuchsia_settings::ConfigurationInterfaces::WIFI) {
            flags |= ConfigurationInterfaceFlags::WIFI;
        }

        flags
    }
}

impl From<ConfigurationInterfaceFlags> for fidl_fuchsia_settings::ConfigurationInterfaces {
    fn from(flags: ConfigurationInterfaceFlags) -> Self {
        let mut interfaces = fidl_fuchsia_settings::ConfigurationInterfaces::empty();

        if flags.intersects(ConfigurationInterfaceFlags::ETHERNET) {
            interfaces |= fidl_fuchsia_settings::ConfigurationInterfaces::ETHERNET;
        }

        if flags.intersects(ConfigurationInterfaceFlags::WIFI) {
            interfaces |= fidl_fuchsia_settings::ConfigurationInterfaces::WIFI;
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

impl request::Responder<Scoped<SetupSetResult>> for SetupSetResponder {
    fn respond(self, Scoped(mut response): Scoped<SetupSetResult>) {
        let _ = self.send(&mut response).ok();
    }
}

impl watch::Responder<SetupSettings, fuchsia_zircon::Status> for SetupWatchResponder {
    fn respond(self, response: Result<SetupSettings, fuchsia_zircon::Status>) {
        match response {
            Ok(settings) => {
                let _ = self.send(settings);
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl TryFrom<SetupRequest> for Job {
    type Error = JobError;
    fn try_from(item: SetupRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            SetupRequest::Set { settings, reboot_device, responder } => {
                match to_request(settings, reboot_device) {
                    Some(request) => {
                        Ok(request::Work::new(SettingType::Setup, request, responder).into())
                    }
                    None => Err(JobError::InvalidInput(Box::new(responder))),
                }
            }
            SetupRequest::Watch { responder } => {
                Ok(watch::Work::new_job(SettingType::Setup, responder))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

fn to_request(settings: SetupSettings, should_reboot: bool) -> Option<Request> {
    if let Some(configuration_interfaces) = settings.enabled_configuration_interfaces {
        return Some(Request::SetConfigurationInterfaces(SetConfigurationInterfacesParams {
            config_interfaces_flags: ConfigurationInterfaceFlags::from(configuration_interfaces),
            should_reboot,
        }));
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::{execution, work};
    use assert_matches::assert_matches;
    use fidl_fuchsia_settings::{SetupMarker, SetupRequestStream};
    use futures::StreamExt;

    #[test]
    fn test_request_from_settings() {
        const CONFIGURATION_INTERFACES: Option<fidl_fuchsia_settings::ConfigurationInterfaces> =
            Some(fidl_fuchsia_settings::ConfigurationInterfaces::ETHERNET);
        const CONFIGURATION_INTERFACE_FLAG: ConfigurationInterfaceFlags =
            ConfigurationInterfaceFlags::ETHERNET;
        const SHOULD_REBOOT: bool = true;

        let mut setup_settings = SetupSettings::EMPTY;
        setup_settings.enabled_configuration_interfaces = CONFIGURATION_INTERFACES;

        let request = to_request(setup_settings, SHOULD_REBOOT);

        assert_eq!(
            request,
            Some(Request::SetConfigurationInterfaces(SetConfigurationInterfacesParams {
                config_interfaces_flags: CONFIGURATION_INTERFACE_FLAG,
                should_reboot: SHOULD_REBOOT,
            }))
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_converts_supplied_params() {
        const CONFIGURATION_INTERFACES: Option<fidl_fuchsia_settings::ConfigurationInterfaces> =
            Some(fidl_fuchsia_settings::ConfigurationInterfaces::ETHERNET);
        const SHOULD_REBOOT: bool = true;

        let (proxy, server) =
            fidl::endpoints::create_proxy::<SetupMarker>().expect("should be able to create proxy");
        let _fut = proxy.set(
            SetupSettings {
                enabled_configuration_interfaces: CONFIGURATION_INTERFACES,
                ..SetupSettings::EMPTY
            },
            SHOULD_REBOOT,
        );
        let mut request_stream: SetupRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        let job = job.as_ref();
        assert_matches!(job.map(|j| j.workload()), Ok(work::Load::Independent(_)));
        assert_matches!(job.map(|j| j.execution_type()), Ok(execution::Type::Independent));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_watch_converts_supplied_params() {
        let (proxy, server) =
            fidl::endpoints::create_proxy::<SetupMarker>().expect("should be able to create proxy");
        let _fut = proxy.watch();
        let mut request_stream: SetupRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        let job = job.as_ref();
        assert_matches!(job.map(|j| j.workload()), Ok(work::Load::Sequential(_, _)));
        assert_matches!(job.map(|j| j.execution_type()), Ok(execution::Type::Sequential(_)));
    }
}
