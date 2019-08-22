// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::*,
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

pub fn spawn_system_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: SystemRequestStream,
) {
    let switchboard_lock = switchboard_handle.clone();

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
                    let (response_tx, response_rx) =
                        futures::channel::oneshot::channel::<SettingResponseResult>();
                    {
                        let mut switchboard = switchboard_lock.write().unwrap();
                        switchboard
                            .request(SettingType::System, SettingRequest::Get, response_tx)
                            .unwrap();
                    }

                    // TODO(go/fxb/35307): Support hanging get.
                    if let Ok(Some(SettingResponse::System(info))) = response_rx.await.unwrap() {
                        let mut system_settings = fidl_fuchsia_settings::SystemSettings::empty();
                        system_settings.mode = Some(fidl_fuchsia_settings::LoginOverride::from(
                            info.login_override_mode,
                        ));
                        responder.send(&mut Ok(system_settings)).unwrap();
                    } else {
                        panic!("incorrect value sent to system");
                    }
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
        .unwrap()
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
