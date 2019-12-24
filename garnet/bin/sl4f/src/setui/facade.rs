// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};

use serde_json::{from_value, to_value, Value};

use crate::setui::types::{JsonMutation, LoginOverrideMode, SetUiResult};
use fidl_fuchsia_setui::*;
use fuchsia_component::client::connect_to_service;

/// Facade providing access to SetUi interfaces.
#[derive(Debug)]
pub struct SetUiFacade {
    setui_svc: SetUiServiceProxy,
}

impl SetUiFacade {
    pub fn new() -> Result<SetUiFacade, Error> {
        let setui_svc =
            connect_to_service::<SetUiServiceMarker>().expect("Failed to connect to SetUi");
        Ok(SetUiFacade { setui_svc })
    }

    /// Sets the value of a given settings object. Returns once operation has completed.
    pub async fn mutate(&self, args: Value) -> Result<Value, Error> {
        let json_mutation: JsonMutation = from_value(args)?;
        print!("{:?}", json_mutation);

        let mut mutation: Mutation;
        let setting_type: SettingType;
        match json_mutation {
            JsonMutation::Account { operation: _, login_override } => {
                // TODO(isma): Is there a way to just use the fidl enum?
                let login_override: LoginOverride = match login_override {
                    LoginOverrideMode::None => LoginOverride::None,
                    LoginOverrideMode::AutologinGuest => LoginOverride::AutologinGuest,
                    LoginOverrideMode::AuthProvider => LoginOverride::AuthProvider,
                };
                mutation = Mutation::AccountMutationValue(AccountMutation {
                    operation: Some(AccountOperation::SetLoginOverride),
                    login_override: Some(login_override),
                });
                setting_type = SettingType::Account;
            }
        }
        match self.setui_svc.mutate(setting_type, &mut mutation).await?.return_code {
            ReturnCode::Ok => Ok(to_value(SetUiResult::Success)?),
            ReturnCode::Failed => return Err(format_err!("Update settings failed")),
            ReturnCode::Unsupported => return Err(format_err!("Update settings unsupported")),
        }
    }
}
