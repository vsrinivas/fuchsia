// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl_fuchsia_setui::*,
};

/// Converts argument string into a known SettingType. Will return an error if
/// no suitable type can be determined.
fn extract_setting_type(s: &str) -> Result<SettingType, Error> {
    match s {
        "unknown" => Ok(SettingType::Unknown),
        "login" => Ok(SettingType::Account),
        _ => Err(failure::format_err!("unknown type:{}", s)),
    }
}

/// Generates the appropriate Mutation for a string change.
fn generate_string_mutation(value: &str) -> Result<Mutation, Error> {
    Ok(fidl_fuchsia_setui::Mutation::StringMutationValue(StringMutation {
        operation: StringOperation::Update,
        value: value.to_string(),
    }))
}

/// Converts the user-specified login override value into the equivalent
/// account mutation.
fn generate_account_mutation(value: &str) -> Result<Mutation, Error> {
    Ok(Mutation::AccountMutationValue(AccountMutation {
        operation: Some(AccountOperation::SetLoginOverride),
        login_override: Some(extract_login_override(value)?),
    }))
}

/// Converts the user-specified login override value into the defined
/// LoginOverride enum value.
fn extract_login_override(value: &str) -> Result<LoginOverride, Error> {
    match value {
        "auth" => Ok(LoginOverride::AuthProvider),
        "autologinguest" => Ok(LoginOverride::AutologinGuest),
        "none" => Ok(LoginOverride::None),
        _ => Err(format_err!("unknown login override")),
    }
}

/// Retrieves the current setting for the spcified type.
pub async fn get(setui: SetUiServiceProxy, setting_type: String) -> Result<SettingsObject, Error> {
    let converted_type = extract_setting_type(&setting_type)?;
    let setting = await!(setui.watch(converted_type))?;
    Ok(setting)
}

pub async fn mutate(
    setui: SetUiServiceProxy,
    setting_type: String,
    value: String,
) -> Result<(), Error> {
    let converted_type = extract_setting_type(&setting_type)?;

    match converted_type {
        SettingType::Unknown => {
            let mut mutation = generate_string_mutation(&value)?;
            await!(setui.mutate(converted_type, &mut mutation))?;
        }
        SettingType::Account => {
            let mut mutation = generate_account_mutation(&value)?;
            await!(setui.mutate(converted_type, &mut mutation))?;
        }
        _ => {
            return Err(format_err!("unsupported type"));
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_setting_type() {
        assert_eq!(extract_setting_type("unknown").unwrap(), SettingType::Unknown);
        assert!(extract_setting_type("outside").is_err(), "outside isn't a valid type");
    }

    #[test]
    fn test_generate_string_mutation() {
        let test_string = "test_string";
        match generate_string_mutation(test_string).unwrap() {
            fidl_fuchsia_setui::Mutation::StringMutationValue(mutation) => {
                assert_eq!(mutation.value, test_string);
                assert_eq!(mutation.operation, StringOperation::Update);
            }
            _ => assert!(false, "generated unknown type"),
        }
    }

    #[test]
    fn test_generate_account_mutation() {
        validate_mutation_generation(&"auth".to_string(), LoginOverride::AuthProvider);
        validate_mutation_generation(&"autologinguest".to_string(), LoginOverride::AutologinGuest);
    }

    fn validate_mutation_generation(value: &str, override_type: LoginOverride) {
        match generate_account_mutation(value).unwrap() {
            fidl_fuchsia_setui::Mutation::AccountMutationValue(mutation) => {
                if let (Some(login_override), Some(operation)) =
                    (mutation.login_override, mutation.operation)
                {
                    assert_eq!(login_override, override_type);
                    assert_eq!(operation, AccountOperation::SetLoginOverride);
                } else {
                    assert!(false, "login override not present");
                }
            }
            _ => assert!(false, "generated unknown type"),
        }
    }
}
