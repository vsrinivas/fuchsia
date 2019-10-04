// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    failure::format_err,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
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
                        change_login_override(
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

/// Sets the login mode and schedules accounts to be cleared. Upon success, the
/// device is scheduled to reboot so the change will take effect.
fn change_login_override(
    switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    mode: SystemLoginOverrideMode,
    responder: SystemSetResponder,
) {
    fasync::spawn(async move {
        let login_override_result = request(
            switchboard.clone(),
            SettingType::System,
            SettingRequest::SetLoginOverrideMode(mode),
            "set login override",
        )
        .await;
        if login_override_result.is_err() {
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            return;
        }

        let schedule_account_clear_result = request(
            switchboard.clone(),
            SettingType::Account,
            SettingRequest::ScheduleClearAccounts,
            "clear accounts",
        )
        .await;

        if schedule_account_clear_result.is_err() {
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            return;
        }

        let restart_result =
            request(switchboard.clone(), SettingType::Power, SettingRequest::Reboot, "rebooting")
                .await;

        if restart_result.is_err() {
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            return;
        }

        responder.send(&mut Ok(())).ok();
    });
}

async fn request(
    switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    setting_type: SettingType,
    setting_request: SettingRequest,
    description: &str,
) -> SettingResponseResult {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    let result = switchboard.clone().write().request(setting_type, setting_request, response_tx);

    match result {
        Ok(()) => match response_rx.await {
            Ok(result) => {
                return result;
            }
            Err(error) => {
                fx_log_err!("request failed: {} error: {}", description, error);
                return Err(format_err!("request failed: {} error: {}", description, error));
            }
        },
        Err(error) => {
            fx_log_err!("request failed: {} error: {}", description, error);
            return Err(error);
        }
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
