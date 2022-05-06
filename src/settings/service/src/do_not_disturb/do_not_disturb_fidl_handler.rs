// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::fidl_common::FidlResponseErrorLogger;
use crate::fidl_hanging_get_responder;
use crate::fidl_process;
use crate::fidl_processor::settings::RequestContext;
use crate::handler::base::Request;
use crate::request_respond;
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_settings::{
    DoNotDisturbMarker, DoNotDisturbRequest, DoNotDisturbSettings, DoNotDisturbWatchResponder,
    Error,
};
use fuchsia_async as fasync;

fidl_hanging_get_responder!(DoNotDisturbMarker, DoNotDisturbSettings, DoNotDisturbWatchResponder,);

impl From<SettingInfo> for DoNotDisturbSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::DoNotDisturb(info) = response {
            let mut dnd_settings = fidl_fuchsia_settings::DoNotDisturbSettings::EMPTY;
            dnd_settings.user_initiated_do_not_disturb = info.user_dnd;
            dnd_settings.night_mode_initiated_do_not_disturb = info.night_mode_dnd;
            dnd_settings
        } else {
            panic!("incorrect value sent to do_not_disturb");
        }
    }
}

fn to_request(settings: DoNotDisturbSettings) -> Option<Request> {
    let mut dnd_info = DoNotDisturbInfo::empty();
    dnd_info.user_dnd = settings.user_initiated_do_not_disturb;
    dnd_info.night_mode_dnd = settings.night_mode_initiated_do_not_disturb;
    Some(Request::SetDnD(dnd_info))
}

fidl_process!(DoNotDisturb, SettingType::DoNotDisturb, process_request);

async fn process_request(
    context: RequestContext<DoNotDisturbSettings, DoNotDisturbWatchResponder>,
    req: DoNotDisturbRequest,
) -> Result<Option<DoNotDisturbRequest>, anyhow::Error> {
    // Support future expansion of FIDL
    #[allow(unreachable_patterns)]
    match req {
        DoNotDisturbRequest::Set { settings, responder } => {
            if let Some(request) = to_request(settings) {
                fasync::Task::spawn(async move {
                    request_respond!(
                        context,
                        responder,
                        SettingType::DoNotDisturb,
                        request,
                        Ok(()),
                        Err(Error::Failed),
                        DoNotDisturbMarker
                    );
                })
                .detach();
            } else {
                responder
                    .send(&mut Err(Error::Failed))
                    .log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME);
            }
        }
        DoNotDisturbRequest::Watch { responder } => context.watch(responder, true).await,
        _ => {
            return Ok(Some(req));
        }
    }

    Ok(None)
}
