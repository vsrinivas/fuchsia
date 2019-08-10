// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::base::Command;
use crate::switchboard::base::{
    IntlInfo, SettingRequest, SettingRequestResponder, SettingResponse,
};
use failure::{format_err, Error};
use fidl_fuchsia_timezone::*;
use fuchsia_async as fasync;
use futures::StreamExt;
use futures::TryFutureExt;
use std::sync::{Arc, RwLock};

pub struct IntlController {
    proxy: TimezoneProxy,
}

/// Controller for processing switchboard messages surrounding the Intl
/// protocol, backed by a number of services, including TimeZone.
impl IntlController {
    pub fn spawn(
        proxy: TimezoneProxy,
    ) -> Result<futures::channel::mpsc::UnboundedSender<Command>, Error> {
        let handle = Arc::new(RwLock::new(Self { proxy: proxy }));

        let (ctrl_tx, mut ctrl_rx) = futures::channel::mpsc::unbounded::<Command>();

        let handle_clone = handle.clone();
        fasync::spawn(
            async move {
                while let Some(command) = ctrl_rx.next().await {
                    handle_clone.write().unwrap().process_command(command);
                }
                Ok(())
            }
                .unwrap_or_else(|_e: failure::Error| {}),
        );

        return Ok(ctrl_tx);
    }

    fn process_command(&self, command: Command) {
        match command {
            Command::HandleRequest(request, responder) => match request {
                SettingRequest::SetTimeZone(id) => {
                    self.set_time_zone(id, responder);
                }
                SettingRequest::Get => {
                    self.get(responder);
                }
                _ => {
                    responder.send(Err(format_err!("unimplemented"))).ok();
                }
            },
            Command::ChangeState(_state) => {
                // For now, ignore all state changes.
            }
        }
    }

    fn get(&self, responder: SettingRequestResponder) {
        let proxy = self.proxy.clone();
        fasync::spawn(
            async move {
                if let Ok(id) = proxy.get_timezone_id().await {
                    responder
                        .send(Ok(Some(SettingResponse::Intl(IntlInfo { time_zone_id: id }))))
                        .ok();
                } else {
                    responder.send(Err(format_err!("get time zone failed"))).ok();
                }
                Ok(())
            }
                .unwrap_or_else(|_e: failure::Error| {}),
        );
    }

    fn set_time_zone(&self, time_zone_id: String, responder: SettingRequestResponder) {
        let proxy = self.proxy.clone();
        fasync::spawn(
            async move {
                if let Ok(true) = proxy.set_timezone(time_zone_id.as_str()).await {
                    responder.send(Ok(None)).ok();
                } else {
                    responder.send(Err(format_err!("set time zone failed"))).ok();
                }
                Ok(())
            }
                .unwrap_or_else(|_e: failure::Error| {}),
        );
    }
}
