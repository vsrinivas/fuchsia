// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_settings::*,
};

pub const LOGIN_OVERRIDE_AUTH: &str = "auth";
pub const LOGIN_OVERRIDE_AUTOLOGINGUEST: &str = "autologinguest";
pub const LOGIN_OVERRIDE_NONE: &str = "none";

pub async fn command(proxy: SystemProxy, login_override: Option<String>) -> Result<String, Error> {
    let mut output = String::new();

    match login_override {
        Some(override_value) => {
            let mut settings = SystemSettings::empty();
            settings.mode = Some(extract_login_override(&override_value)?);

            let mutate_result = proxy.set(settings).await?;
            match mutate_result {
                Ok(()) => output.push_str(&format!("Successfully set to {}", override_value)),
                Err(err) => output.push_str(&format!("{:?}", err)),
            }
        }
        None => {
            let setting = proxy.watch().await?;

            match setting {
                Ok(setting) => {
                    let setting_string = describe_login_override(setting.mode)?;
                    output.push_str(&setting_string);
                }
                Err(err) => output.push_str(&format!("{:?}", err)),
            }
        }
    }

    Ok(output)
}

/// Converts the user-specified login override value into the defined
/// LoginOverride enum value.
fn extract_login_override(value: &str) -> Result<LoginOverride, Error> {
    match value {
        LOGIN_OVERRIDE_AUTH => Ok(LoginOverride::AuthProvider),
        LOGIN_OVERRIDE_AUTOLOGINGUEST => Ok(LoginOverride::AutologinGuest),
        LOGIN_OVERRIDE_NONE => Ok(LoginOverride::None),
        _ => Err(format_err!("unknown login override")),
    }
}

fn describe_login_override(login_override_option: Option<LoginOverride>) -> Result<String, Error> {
    if login_override_option == None {
        return Ok("none".to_string());
    }

    match login_override_option.unwrap() {
        LoginOverride::AutologinGuest => Ok(LOGIN_OVERRIDE_AUTOLOGINGUEST.to_string()),
        LoginOverride::None => Ok(LOGIN_OVERRIDE_NONE.to_string()),
        LoginOverride::AuthProvider => Ok(LOGIN_OVERRIDE_AUTH.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Verifies that externally dependent values are not changed
    #[test]
    fn test_describe_account_override() {
        verify_account_override(LoginOverride::AutologinGuest, "autologinguest");
        verify_account_override(LoginOverride::None, "none");
        verify_account_override(LoginOverride::AuthProvider, "auth");
    }

    fn verify_account_override(login_override: LoginOverride, expected: &str) {
        match describe_login_override(Some(login_override)) {
            Ok(description) => {
                assert_eq!(description, expected);
            }
            _ => {
                panic!("expected");
            }
        }
    }
}
