// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_hanging_get_responder,
    crate::fidl_hanging_get_result_responder,
    crate::fidl_processor::{process_stream_both_watches, RequestContext},
    crate::switchboard::base::{
        SettingRequest, SettingResponse, SettingType, SwitchboardClient, SystemLoginOverrideMode,
    },
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{
        SystemMarker, SystemRequest, SystemRequestStream, SystemSetResponder, SystemSettings,
        SystemWatch2Responder, SystemWatchResponder,
    },
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
};

fidl_hanging_get_responder!(SystemSettings, SystemWatch2Responder, SystemMarker::DEBUG_NAME);

// TODO(fxb/52593): Remove when clients are ported to watch2.
fidl_hanging_get_result_responder!(SystemSettings, SystemWatchResponder, SystemMarker::DEBUG_NAME);

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
    switchboard_client: SwitchboardClient,
    stream: SystemRequestStream,
) {
    // TODO(fxb/52593): Convert back to process_stream when clients are ported to watch2.
    process_stream_both_watches::<
        SystemMarker,
        SystemSettings,
        SystemWatchResponder,
        SystemWatch2Responder,
    >(
        stream,
        switchboard_client,
        SettingType::System,
        // Separate handlers because there are two separate Responders for Watch and
        // Watch2. The hanging get handlers can only handle one type of Responder
        // at a time, so they must be registered separately.
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<SystemRequest>, anyhow::Error>> {
                async move {
                    #[allow(unreachable_patterns)]
                    match req {
                        SystemRequest::Watch { responder } => {
                            context.watch(responder, false).await;
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
                        SystemRequest::Watch2 { responder } => {
                            context.watch(responder, true).await;
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
    context: RequestContext<SystemSettings, SystemWatch2Responder>,
    mode: SystemLoginOverrideMode,
    responder: SystemSetResponder,
) {
    fasync::spawn(async move {
        let login_override_result = request(
            &context.switchboard_client,
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
            &context.switchboard_client,
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
            &context.switchboard_client,
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
    switchboard_client: &SwitchboardClient,
    setting_type: SettingType,
    setting_request: SettingRequest,
    description: &str,
) -> Result<(), ()> {
    match switchboard_client.request(setting_type, setting_request).await {
        Ok(response_rx) => response_rx
            .await
            .map_err(|error| error.to_string())
            .and_then(|resp| resp.map(|_| {}).map_err(|error| error.to_string()))
            .map_err(|error| {
                fx_log_err!("request failed: {} error: {}", description, error);
            }),
        Err(error) => {
            fx_log_err!("request failed: {} error: {}", description, error);
            return Err(());
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
