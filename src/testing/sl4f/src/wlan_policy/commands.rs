// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        server::Facade, wlan_policy::ap_facade::WlanApPolicyFacade,
        wlan_policy::facade::WlanPolicyFacade,
    },
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_syslog::macros::*,
    serde_json::{to_value, Value},
};

#[async_trait(?Send)]
impl Facade for WlanPolicyFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "scan_for_networks" => {
                fx_log_info!(tag: "WlanPolicyFacade", "performing scan for networks");
                let result = self.scan_for_networks().await?;
                to_value(result).map_err(|e| format_err!("error handling scan result: {}", e))
            }
            "connect" => {
                let target_ssid = parse_target_ssid(&args)?;
                let security_type = parse_security_type(&args)?;

                fx_log_info!(tag: "WlanPolicyFacade", "performing wlan connect to SSID: {:?}", target_ssid);
                let result = self.connect(target_ssid, security_type).await?;
                to_value(result).map_err(|e| format_err!("error parsing connection result: {}", e))
            }
            "remove_network" => {
                let target_ssid = parse_target_ssid(&args)?;
                let security_type = parse_security_type(&args)?;
                let target_pwd = parse_target_pwd(&args)?;

                fx_log_info!(tag: "WlanPolicyFacade", "removing network with SSID: {:?}", target_ssid);
                let result = self.remove_network(target_ssid, security_type, target_pwd).await?;
                to_value(result)
                    .map_err(|e| format_err!("error parsing remove network result: {}", e))
            }
            "start_client_connections" => {
                fx_log_info!(tag: "WlanPolicyFacade", "attempting to start client connections");
                let result = self.start_client_connections().await?;
                to_value(result).map_err(|e| {
                    format_err!("error handling start client connections result: {}", e)
                })
            }
            "stop_client_connections" => {
                fx_log_info!(tag: "WlanPolicyFacade", "attempting to stop client connections");
                let result = self.stop_client_connections().await?;
                to_value(result).map_err(|e| {
                    format_err!("error handling stop client connections result: {}", e)
                })
            }
            "save_network" => {
                let target_ssid = parse_target_ssid(&args)?;
                let security_type = parse_security_type(&args)?;
                let target_pwd = parse_target_pwd(&args)?;

                fx_log_info!(tag: "WlanPolicyFacade", "saving network with SSID: {:?}", target_ssid);
                let result = self.save_network(target_ssid, security_type, target_pwd).await?;
                to_value(result)
                    .map_err(|e| format_err!("error parsing save network result: {}", e))
            }
            "get_saved_networks" => {
                fx_log_info!(tag: "WlanPolicyFacade", "attempting to get saved networks");
                let result = self.get_saved_networks_json().await?;
                to_value(result)
                    .map_err(|e| format_err!("error handling get saved networks result: {}", e))
            }
            "create_client_controller" => {
                fx_log_info!(tag: "WlanPolicyFacade", "initializing client controller");
                let result = self.create_client_controller()?;
                to_value(result)
                    .map_err(|e| format_err!("error initializing client controller: {}", e))
            }
            "remove_all_networks" => {
                fx_log_info!(tag: "WlanPolicyFacade", "Removing all saved client network configs");
                let result = self.remove_all_networks().await?;
                to_value(result)
                    .map_err(|e| format_err!("error removing all saved networks: {}", e))
            }
            "get_update" => {
                fx_log_info!(tag: "WlanPolicyFacade", "getting client update");
                let result = self.get_update().await?;
                to_value(result).map_err(|e| format_err!("error handling listener update: {}", e))
            }
            "set_new_update_listener" => {
                fx_log_info!(tag: "WlanPolicyFacade", "initializing new update listener");
                let result = self.set_new_listener()?;
                to_value(result)
                    .map_err(|e| format_err!("error initializing new update listener: {}", e))
            }
            _ => return Err(format_err!("unsupported command!")),
        }
    }
}

fn parse_target_ssid(args: &Value) -> Result<Vec<u8>, Error> {
    args.get("target_ssid")
        .and_then(|ssid| ssid.as_str().map(|ssid| ssid.as_bytes().to_vec()))
        .ok_or(format_err!("Please provide a target ssid"))
}

/// In ACTS tests we will require a security type is specified for a call that uses a security
/// type; none specified will not default to a none security type.
fn parse_security_type(args: &Value) -> Result<fidl_policy::SecurityType, Error> {
    let security_type = match args.get("security_type") {
        Some(Value::String(security)) => security.as_bytes().to_vec(),
        Some(value) => {
            fx_log_info!(tag: "WlanFacade", "Please check provided security type, must be String");
            bail!("provided security type arg is not a string, cannot parse {}", value);
        }
        None => {
            fx_log_info!(tag: "WlanFacade", "Please check provided security type, none found");
            bail!("no security type is provided");
        }
    };

    // Parse network ID to connect to. The string is made lower case upstream in the pipeline.
    match std::str::from_utf8(&security_type)? {
        "none" => Ok(fidl_policy::SecurityType::None),
        "wep" => Ok(fidl_policy::SecurityType::Wep),
        "wpa" => Ok(fidl_policy::SecurityType::Wpa),
        "wpa2" => Ok(fidl_policy::SecurityType::Wpa2),
        "wpa3" => Ok(fidl_policy::SecurityType::Wpa3),
        _ => Err(format_err!("failed to parse security type (None, WEP, WPA, WPA2, or WPA3")),
    }
}

/// Parse the credential argument. The credential argument must be a string. No credential (for an
/// open network) must be indicated by an empty string. Tests may omit the password argument, and
/// if so an empty string will be provided as a default value as an argument. Tests do not need
/// to specify the type of credential; it will be infered by the length.
/// PSK format must be string representation of hexidecimal, not the 32 bytes representation.
/// PSK will be distinguished from password by the length of password, if 64 bytes it will be PSK.
fn parse_target_pwd(args: &Value) -> Result<fidl_policy::Credential, Error> {
    let target_pwd = match args.get("target_pwd") {
        Some(Value::String(pwd)) => pwd.as_bytes().to_vec(),
        Some(value) => {
            fx_log_info!(tag: "WlanFacade", "Please check provided credential, must be String");
            bail!("provided credential is not a string, cannot parse {}", value);
        }
        None => {
            fx_log_info!(tag: "WlanFacade", "Please check provided credential, none provided");
            bail!("no credential argument provided");
        }
    };

    const PSK_LEN: usize = 64;
    let credential = match target_pwd.len() {
        0 => fidl_policy::Credential::None(fidl_policy::Empty),
        PSK_LEN => {
            let psk = hex::decode(target_pwd).map_err(|e| {
                fx_log_info!(
                    tag: "WlanFacade",
                    "Please check provided credential, PSK must be valid hexadecimal string"
                );
                format_err!("provided credential length matches PSK, failed to decode: {:?}", e)
            })?;
            fidl_policy::Credential::Psk(psk)
        }
        _ => fidl_policy::Credential::Password(target_pwd),
    };
    Ok(credential)
}

fn extract_operating_band(args: &Value) -> Result<fidl_fuchsia_wlan_policy::OperatingBand, Error> {
    match args.get("operating_band") {
        Some(operating_band) => match operating_band.as_str() {
            Some(operating_band) => match operating_band.to_lowercase().as_str() {
                "any" => Ok(fidl_fuchsia_wlan_policy::OperatingBand::Any),
                "only_2_4_ghz" => Ok(fidl_fuchsia_wlan_policy::OperatingBand::Only24Ghz),
                "only_5_ghz" => Ok(fidl_fuchsia_wlan_policy::OperatingBand::Only5Ghz),
                _ => Err(format_err!("invalid operating band: {:?}", operating_band)),
            },
            None => Err(format_err!("operating band must be a string")),
        },
        None => Err(format_err!("operating band was not specified")),
    }
}

fn extract_connectivity_mode(
    args: &Value,
) -> Result<fidl_fuchsia_wlan_policy::ConnectivityMode, Error> {
    match args.get("connectivity_mode") {
        Some(connectivity_mode) => match connectivity_mode.as_str() {
            Some(connectivity_mode) => match connectivity_mode.to_lowercase().as_str() {
                "local_only" => Ok(fidl_fuchsia_wlan_policy::ConnectivityMode::LocalOnly),
                "unrestricted" => Ok(fidl_fuchsia_wlan_policy::ConnectivityMode::Unrestricted),
                _ => Err(format_err!("unsupported connectivity mode: {}", connectivity_mode)),
            },
            None => Err(format_err!("connectivity mode must be a string")),
        },
        None => Err(format_err!("no connectivity mode specified")),
    }
}

#[async_trait(?Send)]
impl Facade for WlanApPolicyFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "start_access_point" => {
                let target_ssid = parse_target_ssid(&args)?;
                let security_type = parse_security_type(&args)?;
                let target_pwd = parse_target_pwd(&args)?;

                let connectivity_mode = extract_connectivity_mode(&args)?;
                let operating_band = extract_operating_band(&args)?;
                self.start_access_point(
                    target_ssid,
                    security_type,
                    target_pwd,
                    connectivity_mode,
                    operating_band,
                )
                .await?;
                return Ok(Value::Bool(true));
            }
            "stop_access_point" => {
                let target_ssid = parse_target_ssid(&args)?;
                let security_type = parse_security_type(&args)?;
                let target_pwd = parse_target_pwd(&args)?;
                self.stop_access_point(target_ssid, security_type, target_pwd).await?;
                return Ok(Value::Bool(true));
            }
            "stop_all_access_points" => {
                self.stop_all_access_points().await?;
                return Ok(Value::Bool(true));
            }
            _ => {
                return Err(format_err!("Unsupported command"));
            }
        }
    }
}
