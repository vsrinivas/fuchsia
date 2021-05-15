// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::{
    Error, NightModeMarker, NightModeRequest, NightModeSettings, NightModeWatchResponder,
};
use fuchsia_async as fasync;

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::fidl_process;
use crate::fidl_processor::settings::RequestContext;
use crate::handler::base::Request;
use crate::night_mode::types::NightModeInfo;
use crate::request_respond;

fidl_hanging_get_responder!(NightModeMarker, NightModeSettings, NightModeWatchResponder,);

impl From<SettingInfo> for NightModeSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::NightMode(info) = response {
            let mut night_mode_settings = NightModeSettings::EMPTY;
            night_mode_settings.night_mode_enabled = info.night_mode_enabled;
            return night_mode_settings;
        }

        panic!("incorrect value sent to night_mode");
    }
}

impl From<NightModeSettings> for Request {
    fn from(settings: NightModeSettings) -> Self {
        let mut night_mode_info = NightModeInfo::empty();
        night_mode_info.night_mode_enabled = settings.night_mode_enabled;
        Request::SetNightModeInfo(night_mode_info)
    }
}

fidl_process!(NightMode, SettingType::NightMode, process_request);

async fn process_request(
    context: RequestContext<NightModeSettings, NightModeWatchResponder>,
    req: NightModeRequest,
) -> Result<Option<NightModeRequest>, anyhow::Error> {
    #[allow(unreachable_patterns)]
    match req {
        NightModeRequest::Set { settings, responder } => {
            fasync::Task::spawn(async move {
                request_respond!(
                    context,
                    responder,
                    SettingType::NightMode,
                    settings.into(),
                    Ok(()),
                    Err(Error::Failed),
                    NightModeMarker
                );
            })
            .detach();
        }
        NightModeRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    Ok(None)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_request_from_settings_empty() {
        let request = Request::from(NightModeSettings::EMPTY);
        let night_mode_info = NightModeInfo::empty();
        assert_eq!(request, Request::SetNightModeInfo(night_mode_info));
    }

    #[test]
    fn test_request_from_settings() {
        const NIGHT_MODE_ENABLED: bool = true;

        let mut night_mode_settings = NightModeSettings::EMPTY;
        night_mode_settings.night_mode_enabled = Some(NIGHT_MODE_ENABLED);
        let request = Request::from(night_mode_settings);

        let mut night_mode_info = NightModeInfo::empty();
        night_mode_info.night_mode_enabled = Some(NIGHT_MODE_ENABLED);

        assert_eq!(request, Request::SetNightModeInfo(night_mode_info));
    }
}
