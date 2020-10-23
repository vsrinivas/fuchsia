// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_processor::settings::RequestContext;
use crate::switchboard::base::{
    FidlResponseErrorLogger, SettingRequest, SettingResponse, SettingType,
};
use crate::{fidl_hanging_get_responder, fidl_process, request_respond};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    Error, FactoryResetMarker, FactoryResetRequest, FactoryResetSettings,
    FactoryResetWatchResponder,
};
use fuchsia_async as fasync;

fidl_hanging_get_responder!(FactoryResetMarker, FactoryResetSettings, FactoryResetWatchResponder,);

impl From<SettingResponse> for FactoryResetSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::FactoryReset(info) = response {
            let mut factory_reset_settings = FactoryResetSettings::empty();
            factory_reset_settings.is_local_reset_allowed = Some(info.is_local_reset_allowed);
            factory_reset_settings
        } else {
            panic!("incorrect value sent to factory_reset");
        }
    }
}

fn to_request(settings: FactoryResetSettings) -> Option<SettingRequest> {
    settings.is_local_reset_allowed.map(SettingRequest::SetLocalResetAllowed)
}

fidl_process!(FactoryReset, SettingType::FactoryReset, process_request);

async fn process_request(
    context: RequestContext<FactoryResetSettings, FactoryResetWatchResponder>,
    req: FactoryResetRequest,
) -> Result<Option<FactoryResetRequest>, anyhow::Error> {
    // Support future expansion of FIDL
    #[allow(unreachable_patterns)]
    match req {
        FactoryResetRequest::Set { settings, responder } => {
            if let Some(request) = to_request(settings) {
                fasync::Task::spawn(async move {
                    request_respond!(
                        context,
                        responder,
                        SettingType::FactoryReset,
                        request,
                        Ok(()),
                        Err(fidl_fuchsia_settings::Error::Failed),
                        FactoryResetMarker
                    );
                })
                .detach();
            } else {
                responder
                    .send(&mut Err(Error::Unsupported))
                    .log_fidl_response_error(FactoryResetMarker::DEBUG_NAME);
            }
        }
        FactoryResetRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn to_request_maps_correctly() {
        let result = to_request(FactoryResetSettings { is_local_reset_allowed: Some(true) });
        matches::assert_matches!(result, Some(SettingRequest::SetLocalResetAllowed(true)));
    }
}
