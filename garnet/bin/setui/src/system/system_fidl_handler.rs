// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_processor::{process_stream, RequestContext},
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender,
    anyhow::format_err,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
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
    switchboard_handle: SwitchboardHandle,
    stream: SystemRequestStream,
) {
    process_stream::<SystemMarker, SystemSettings, SystemWatchResponder>(
        stream,
        switchboard_handle,
        SettingType::System,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<SystemRequest>, anyhow::Error>> {
                async move {
                    #[allow(unreachable_patterns)]
                    match req {
                        SystemRequest::Set { settings, responder } => {
                            if let Some(mode) = settings.mode {
                                change_login_override(
                                    context.clone(),
                                    SystemLoginOverrideMode::from(mode),
                                    responder,
                                );
                            }
                        }
                        SystemRequest::Watch { responder } => {
                            context.watch(responder).await;
                        }
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            },
        ),
    );
}

/// Sets the login mode and schedules accounts to be cleared. Upon success, the
/// device is scheduled to reboot so the change will take effect.
fn change_login_override(
    context: RequestContext<SystemSettings, SystemWatchResponder>,
    mode: SystemLoginOverrideMode,
    responder: SystemSetResponder,
) {
    fasync::spawn(async move {
        let login_override_result = request(
            context.switchboard.clone(),
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
            context.switchboard.clone(),
            SettingType::Account,
            SettingRequest::ScheduleClearAccounts,
            "clear accounts",
        )
        .await;

        if schedule_account_clear_result.is_err() {
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            return;
        }

        let restart_result = request(
            context.switchboard.clone(),
            SettingType::Power,
            SettingRequest::Reboot,
            "rebooting",
        )
        .await;

        if restart_result.is_err() {
            responder.send(&mut Err(fidl_fuchsia_settings::Error::Failed)).ok();
            return;
        }

        responder.send(&mut Ok(())).ok();
    });
}

async fn request(
    switchboard: SwitchboardHandle,
    setting_type: SettingType,
    setting_request: SettingRequest,
    description: &str,
) -> SettingResponseResult {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    let result =
        switchboard.clone().lock().await.request(setting_type, setting_request, response_tx);

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
