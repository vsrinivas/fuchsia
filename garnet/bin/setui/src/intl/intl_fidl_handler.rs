// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::{
    SettingRequest, SettingResponse, SettingResponseResult, SettingType, Switchboard,
};

use fidl_fuchsia_intl::TimeZoneId;
use fidl_fuchsia_settings::{IntlRequest, IntlRequestStream, IntlSetResponder, IntlSettings};
use futures::TryFutureExt;
use futures::TryStreamExt;
use std::sync::{Arc, RwLock};

use fuchsia_async as fasync;

pub struct IntlFidlHandler {
    switchboard_handle: Arc<RwLock<Switchboard + Send + Sync>>,
}

/// Handler for translating Intl service requests into SetUI switchboard commands.
impl IntlFidlHandler {
    pub fn spawn(
        switchboard: Arc<RwLock<Switchboard + Send + Sync>>,
        mut stream: IntlRequestStream,
    ) {
        let switchboard_lock = switchboard.clone();

        {
            let mut switchboard = switchboard_lock.write().unwrap();
            let (listen_tx, _listen_rx) = futures::channel::mpsc::unbounded::<SettingType>();
            switchboard.listen(SettingType::Intl, listen_tx).unwrap();
        }

        fasync::spawn(async move {
            let handler = Self { switchboard_handle: switchboard.clone() };

            while let Some(req) = stream.try_next().await.unwrap() {
                // Support future expansion of FIDL
                #[allow(unreachable_patterns)]
                match req {
                    IntlRequest::Set { settings, responder } => {
                        if let Some(time_zone_id) = settings.time_zone_id {
                            handler.set_time_zone(time_zone_id, responder);
                        }
                    }
                    IntlRequest::Watch { responder } => {
                        let (response_tx, response_rx) =
                            futures::channel::oneshot::channel::<SettingResponseResult>();

                        {
                            let mut switchboard = switchboard.write().unwrap();

                            switchboard
                                .request(SettingType::Intl, SettingRequest::Get, response_tx)
                                .unwrap();
                        }

                        if let Ok(Some(SettingResponse::Intl(info))) = response_rx.await.unwrap()
                        {
                            let mut intl_settings = IntlSettings::empty();
                            intl_settings.time_zone_id = Some(TimeZoneId { id: info.time_zone_id });

                            responder.send(&mut Ok(intl_settings)).unwrap();
                        }
                    }
                    _ => {}
                }
            }
        })
    }

    fn set_time_zone(&self, time_zone_id: TimeZoneId, responder: IntlSetResponder) {
        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<SettingResponseResult>();
        if self
            .switchboard_handle
            .write()
            .unwrap()
            .request(SettingType::Intl, SettingRequest::SetTimeZone(time_zone_id.id), response_tx)
            .is_ok()
        {
            fasync::spawn(
                async move {
                    // Return success if we get a Ok result from the
                    // switchboard.
                    if let Ok(Ok(_optional_response)) = response_rx.await {
                        responder.send(&mut Ok(())).ok();
                        return Ok(());
                    }
                    responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
                    Ok(())
                }
                    .unwrap_or_else(|_e: failure::Error| {}),
            );
        } else {
            // report back an error immediately if we could not successfully
            // make the time zone set request. The return result can be ignored
            // as there is no actionable steps that can be taken.
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
        }
    }
}
