// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_processor::process_stream,
    crate::fidl_processor::RequestContext,
    crate::internal::switchboard,
    crate::switchboard::base::{SettingRequest, SettingType, SystemLoginOverrideMode},
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
/// This cannot use the fidl_common::fidl_hanging_get_responder since
/// SettingsObject and SystemSettings are defined outside this crate and
/// therefore cannot convert between types.
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

    fn on_error(self) {}
}

pub fn spawn_setui_fidl_handler(
    switchboard_messenger_factory: switchboard::message::Factory,
    stream: SetUiServiceRequestStream,
) {
    process_stream::<SetUiServiceMarker, SystemSettings, SetUiServiceWatchResponder>(
    stream,
    switchboard_messenger_factory,
    SettingType::System,
    Box::new(
      move |context,
            req|
            -> LocalBoxFuture<'_, Result<Option<SetUiServiceRequest>, anyhow::Error>> {
        async move {
          #[allow(unreachable_patterns)]
          match req {
            SetUiServiceRequest::Mutate { setting_type: _, mutation, responder } => {
              if let Mutation::AccountMutationValue(AccountMutation {
                operation: Some(AccountOperation::SetLoginOverride),
                login_override: Some(login_override)
              }) = mutation {
                fasync::spawn(async move {
                    set_login_override(
                    context,
                    SystemLoginOverrideMode::from(login_override),
                    responder,
                    )
                    .await
                });

                return Ok(None);
              }

              responder.send(&mut MutationResponse { return_code: ReturnCode::Failed }).ok();
            }
            SetUiServiceRequest::Watch { setting_type, responder } => {
              if setting_type != fidl_fuchsia_setui::SettingType::Account {
                return Ok(None);
              }

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

async fn set_login_override(
    context: RequestContext<SystemSettings, SetUiServiceWatchResponder>,
    mode: SystemLoginOverrideMode,
    responder: SetUiServiceMutateResponder,
) {
    if let Ok(_) =
        context.request(SettingType::System, SettingRequest::SetLoginOverrideMode(mode)).await
    {
        responder.send(&mut MutationResponse { return_code: ReturnCode::Ok }).ok();
    } else {
        responder.send(&mut MutationResponse { return_code: ReturnCode::Failed }).ok();
    }
}
