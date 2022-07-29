// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use fidl::endpoints::{ControlHandle, Responder};
use fidl_fuchsia_settings::{
    DoNotDisturbRequest, DoNotDisturbSetResponder, DoNotDisturbSetResult, DoNotDisturbSettings,
    DoNotDisturbWatchResponder,
};
use fuchsia_syslog::fx_log_warn;
use std::convert::TryFrom;

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

impl ErrorResponder for DoNotDisturbSetResponder {
    fn id(&self) -> &'static str {
        "DoNotDisturb_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl request::Responder<Scoped<DoNotDisturbSetResult>> for DoNotDisturbSetResponder {
    fn respond(self, Scoped(mut response): Scoped<DoNotDisturbSetResult>) {
        let _ = self.send(&mut response);
    }
}

impl watch::Responder<DoNotDisturbSettings, fuchsia_zircon::Status> for DoNotDisturbWatchResponder {
    fn respond(self, response: Result<DoNotDisturbSettings, fuchsia_zircon::Status>) {
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

fn to_request(settings: DoNotDisturbSettings) -> Request {
    let mut dnd_info = DoNotDisturbInfo::empty();
    dnd_info.user_dnd = settings.user_initiated_do_not_disturb;
    dnd_info.night_mode_dnd = settings.night_mode_initiated_do_not_disturb;
    Request::SetDnD(dnd_info)
}

impl TryFrom<DoNotDisturbRequest> for Job {
    type Error = JobError;
    fn try_from(req: DoNotDisturbRequest) -> Result<Self, Self::Error> {
        // Support future expansion of FIDL
        #[allow(unreachable_patterns)]
        match req {
            DoNotDisturbRequest::Set { settings, responder } => {
                Ok(request::Work::new(SettingType::DoNotDisturb, to_request(settings), responder)
                    .into())
            }
            DoNotDisturbRequest::Watch { responder } => {
                Ok(watch::Work::new_job(SettingType::DoNotDisturb, responder))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", req);
                Err(JobError::Unsupported)
            }
        }
    }
}
