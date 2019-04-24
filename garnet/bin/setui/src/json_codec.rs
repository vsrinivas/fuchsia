// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::SettingCodec;
use failure::{format_err, Error};
use fidl_fuchsia_setui::*;
use serde_json::json;
use serde_json::Value;

pub struct JsonCodec {}

impl JsonCodec {
    pub fn new() -> JsonCodec {
        return JsonCodec {};
    }
}

const KEY_TYPE: &str = "type";
const KEY_DATA: &str = "data";

impl SettingCodec<Value> for JsonCodec {
    fn encode(&self, data: SettingData) -> Result<Value, Error> {
        if let Some(setting_type) = get_type(&data) {
            let mut data_value;
            match setting_type {
                TYPE_STRING_VALUE => {
                    data_value = Some(encode_string_value(&data)?);
                }
                TYPE_ACCOUNT_VALUE => {
                    data_value = Some(encode_account_settings(&data)?);
                }
                _ => {
                    return Err(format_err!("type encoding not available"));
                }
            }

            return Ok(json!({KEY_TYPE: setting_type, KEY_DATA: data_value.unwrap()}));
        } else {
            return Err(format_err!("unhandled data type"));
        }
    }

    fn decode(&self, encoded: Value) -> Result<SettingData, Error> {
        if let Value::Object(mapping) = encoded {
            // Get value mapped to the type key
            if let (Some(type_value), Some(data_value)) =
                (mapping.get(KEY_TYPE), mapping.get(KEY_DATA))
            {
                // Retrieve setting type from value.
                if let Some(setting_type) = type_value.as_u64() {
                    match setting_type {
                        TYPE_STRING_VALUE => return decode_string_value(data_value),
                        TYPE_ACCOUNT_VALUE => return decode_account(data_value),
                        _ => return Err(format_err!("unsupported encoded type")),
                    }
                } else {
                    return Err(format_err!("type is not a number"));
                }
            } else {
                return Err(format_err!("type or data not present"));
            }
        } else {
            return Err(format_err!("root node should be an object"));
        }
    }
}

const TYPE_STRING_VALUE: u64 = 1;
const TYPE_ACCOUNT_VALUE: u64 = 2;

fn get_type(setting_data: &SettingData) -> Option<u64> {
    match setting_data {
        SettingData::StringValue(_val) => Some(TYPE_STRING_VALUE),
        SettingData::Account(_val) => Some(TYPE_ACCOUNT_VALUE),
        _ => None,
    }
}

fn encode_string_value(data: &SettingData) -> Result<Value, Error> {
    if let SettingData::StringValue(string_val) = data {
        return Ok(json!(string_val));
    } else {
        return Err(format_err!("not a string value"));
    }
}

fn decode_string_value(encoded: &Value) -> Result<SettingData, Error> {
    if let Value::String(string_val) = encoded {
        return Ok(SettingData::StringValue(string_val.clone()));
    }

    return Err(format_err!("not a string type"));
}

const ACCOUNT_SETTINGS_MODE_KEY: &str = "mode";

fn encode_account_settings(data: &SettingData) -> Result<Value, Error> {
    if let SettingData::Account(account_settings) = data {
        if account_settings.mode == None {
            return Ok(json!({}));
        }

        let encoded_mode = encode_login_mode(account_settings.mode.unwrap())?;
        return Ok(json!({ ACCOUNT_SETTINGS_MODE_KEY: encoded_mode }));
    } else {
        return Err(format_err!("not an account setting val"));
    }
}

fn decode_account(encoded: &Value) -> Result<SettingData, Error> {
    if let Value::Object(mapping) = encoded {
        match mapping.get(ACCOUNT_SETTINGS_MODE_KEY) {
            Some(val) => {
                Ok(SettingData::Account(AccountSettings { mode: Some(decode_login_mode(val)?) }))
            }
            _ => Ok(SettingData::Account(AccountSettings { mode: None })),
        }
    } else {
        return Err(format_err!("malformed account json"));
    }
}

const LOGIN_MODE_NONE: u64 = 0;
const LOGIN_MODE_GUEST_OVERRIDE: u64 = 1;
const LOGIN_MODE_AUTH_PROVIDER: u64 = 2;

fn encode_login_mode(mode: LoginOverride) -> Result<Value, Error> {
    match mode {
        LoginOverride::None => {
            return Ok(json!(LOGIN_MODE_NONE));
        }
        LoginOverride::AutologinGuest => {
            return Ok(json!(LOGIN_MODE_GUEST_OVERRIDE));
        }
        LoginOverride::AuthProvider => {
            return Ok(json!(LOGIN_MODE_AUTH_PROVIDER));
        }
    }
}

fn decode_login_mode(value: &Value) -> Result<LoginOverride, Error> {
    if let Value::Number(number) = value {
        if let Some(val) = number.as_u64() {
            match val {
                LOGIN_MODE_NONE => {
                    return Ok(LoginOverride::None);
                }
                LOGIN_MODE_GUEST_OVERRIDE => {
                    return Ok(LoginOverride::AutologinGuest);
                }
                LOGIN_MODE_AUTH_PROVIDER => {
                    return Ok(LoginOverride::AuthProvider);
                }
                _ => {
                    return Err(format_err!("not a decodable type"));
                }
            }
        } else {
            return Err(format_err!("incorrect number format"));
        }
    } else {
        return Err(format_err!("not a number"));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A basic test to exercise that basic functionality works. In this case, we
    /// mutate the unknown type, reserved for testing. We should always immediately
    /// receive back an Ok response.
    #[test]
    fn test_encode_string() {
        let test_val = "foo bar";
        let codec = JsonCodec::new();
        let encode_result = codec.encode(SettingData::StringValue(test_val.to_string()));
        assert!(encode_result.is_ok());

        let encoded_value = encode_result.unwrap();
        let decode_result = codec.decode(encoded_value);
        assert!(decode_result.is_ok());

        match decode_result.unwrap() {
            SettingData::StringValue(val) => {
                assert_eq!(val, test_val);
            }
            _ => {
                panic!("wrong type!");
            }
        }
    }

    #[test]
    fn test_encode_account() {
        let codec = JsonCodec::new();
        let test_override = LoginOverride::AutologinGuest;

        let encode_result =
            codec.encode(SettingData::Account(AccountSettings { mode: Some(test_override) }));
        assert!(encode_result.is_ok());

        let encoded_value = encode_result.unwrap();
        let decode_result = codec.decode(encoded_value);
        assert!(decode_result.is_ok());

        match decode_result.unwrap() {
            SettingData::Account(account_settings) => {
                if let Some(login_override) = account_settings.mode {
                    assert_eq!(test_override, login_override);
                } else {
                    panic!("override should have been set");
                }
            }
            _ => {
                panic!("wrong setting data type");
            }
        }
    }
}
