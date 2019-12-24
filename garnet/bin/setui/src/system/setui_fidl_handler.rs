// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_processor::process_stream,
    crate::switchboard::base::{
        SettingRequest, SettingResponseResult, SettingType, SwitchboardHandle,
        SystemLoginOverrideMode,
    },
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::FutureExt,
};

impl From<fidl_fuchsia_setui::LoginOverride> for SystemLoginOverrideMode {
    fn from(login_override: fidl_fuchsia_setui::LoginOverride) -> Self {
        match login_override {
            fidl_fuchsia_setui::LoginOverride::None => SystemLoginOverrideMode::None,
            fidl_fuchsia_setui::LoginOverride::AutologinGuest => {
                SystemLoginOverrideMode::AutologinGuest
            }
            fidl_fuchsia_setui::LoginOverride::AuthProvider => {
                SystemLoginOverrideMode::AuthProvider
            }
        }
    }
}

fn convert_login_override(
    login_override: fidl_fuchsia_settings::LoginOverride,
) -> fidl_fuchsia_setui::LoginOverride {
    match login_override {
        fidl_fuchsia_settings::LoginOverride::None => fidl_fuchsia_setui::LoginOverride::None,
        fidl_fuchsia_settings::LoginOverride::AutologinGuest => {
            fidl_fuchsia_setui::LoginOverride::AutologinGuest
        }
        fidl_fuchsia_settings::LoginOverride::AuthProvider => {
            fidl_fuchsia_setui::LoginOverride::AuthProvider
        }
    }
}

impl Sender<SystemSettings> for SetUiServiceWatchResponder {
    fn send_response(self, data: SystemSettings) {
        let mut mode = None;

        if let Some(login_override) = data.mode {
            mode = Some(convert_login_override(login_override));
        }

        self.send(&mut SettingsObject {
            setting_type: fidl_fuchsia_setui::SettingType::Account,
            data: SettingData::Account(fidl_fuchsia_setui::AccountSettings { mode: mode }),
        })
        .ok();
    }
}

pub fn spawn_setui_fidl_handler(switchboard: SwitchboardHandle, stream: SetUiServiceRequestStream) {
    process_stream::<SetUiServiceMarker, SystemSettings, SetUiServiceWatchResponder>(
    stream,
    switchboard,
    SettingType::System,
    Box::new(
      move |context,
            req|
            -> LocalBoxFuture<'_, Result<Option<SetUiServiceRequest>, anyhow::Error>> {
        async move {
          #[allow(unreachable_patterns)]
          match req {
            SetUiServiceRequest::Mutate { setting_type: _, mutation, responder } => {
              if let Mutation::AccountMutationValue(mutation_info) = mutation {
                if let Some(operation) = mutation_info.operation {
                  if operation == AccountOperation::SetLoginOverride {
                    if let Some(login_override) = mutation_info.login_override {
                      set_login_override(
                        context.switchboard.clone(),
                        SystemLoginOverrideMode::from(login_override),
                        responder,
                      )
                      .await;

                      return Ok(None);
                    }
                  }
                }
              }

              responder.send(&mut MutationResponse { return_code: ReturnCode::Failed }).ok();
            }
            SetUiServiceRequest::Watch { setting_type, responder } => {
              if setting_type != fidl_fuchsia_setui::SettingType::Account {
                return Ok(None);
              }

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

async fn set_login_override(
    switchboard: SwitchboardHandle,
    mode: SystemLoginOverrideMode,
    responder: SetUiServiceMutateResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    if switchboard
        .lock()
        .await
        .request(SettingType::System, SettingRequest::SetLoginOverrideMode(mode), response_tx)
        .is_ok()
    {
        fasync::spawn(async move {
            // Return success if we get a Ok result from the
            // switchboard.
            if let Ok(Ok(_optional_response)) = response_rx.await {
                responder.send(&mut MutationResponse { return_code: ReturnCode::Ok }).ok();
            } else {
                responder.send(&mut MutationResponse { return_code: ReturnCode::Failed }).ok();
            }
        });
    } else {
        responder.send(&mut MutationResponse { return_code: ReturnCode::Failed }).ok();
    }
}
