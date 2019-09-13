// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    futures::lock::Mutex,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

impl Sender<SystemSettings> for SystemWatchResponder {
    fn send_response(self, data: SystemSettings) {
        self.send(&mut Ok(data)).unwrap();
    }
}

impl From<SettingResponse> for SystemSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::System(info) = response {
            let mut system_settings = fidl_fuchsia_settings::SystemSettings::empty();
            system_settings.mode =
                Some(fidl_fuchsia_settings::LoginOverride::from(info.login_override_mode));
            system_settings
        } else {
            panic!("incorrect value sent to system");
        }
    }
}

pub fn spawn_system_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: SystemRequestStream,
) {
    let hanging_get_handler: Arc<Mutex<HangingGetHandler<SystemSettings, SystemWatchResponder>>> =
        HangingGetHandler::create(switchboard_handle.clone(), SettingType::System);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            #[allow(unreachable_patterns)]
            match req {
                SystemRequest::Set { settings, responder } => {
                    if let Some(mode) = settings.mode {
                        set_login_override(
                            switchboard_handle.clone(),
                            SystemLoginOverrideMode::from(mode),
                            responder,
                        );
                    }
                }
                SystemRequest::Watch { responder } => {
                    let mut hanging_get_lock = hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                _ => {}
            }
        }
    });
}

fn set_login_override(
    switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    mode: SystemLoginOverrideMode,
    responder: SystemSetResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    if switchboard
        .write()
        .request(SettingType::System, SettingRequest::SetLoginOverrideMode(mode), response_tx)
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
        responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
    }
}

impl From<fidl_fuchsia_settings::LoginOverride> for SystemLoginOverrideMode {
    fn from(item: fidl_fuchsia_settings::LoginOverride) -> Self {
        match item {
            fidl_fuchsia_settings::LoginOverride::AutologinGuest => {
                SystemLoginOverrideMode::AutologinGuest
            }
            fidl_fuchsia_settings::LoginOverride::AuthProvider => {
                SystemLoginOverrideMode::AuthProvider
            }
            fidl_fuchsia_settings::LoginOverride::None => SystemLoginOverrideMode::None,
        }
    }
}

impl From<SystemLoginOverrideMode> for fidl_fuchsia_settings::LoginOverride {
    fn from(item: SystemLoginOverrideMode) -> Self {
        match item {
            SystemLoginOverrideMode::AutologinGuest => {
                fidl_fuchsia_settings::LoginOverride::AutologinGuest
            }
            SystemLoginOverrideMode::AuthProvider => {
                fidl_fuchsia_settings::LoginOverride::AuthProvider
            }
            SystemLoginOverrideMode::None => fidl_fuchsia_settings::LoginOverride::None,
        }
    }
}
