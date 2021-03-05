// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    crate::error::{FrameParseError, FrameParseResult},
    anyhow::{self, format_err, Context},
    std::convert::TryInto,
    zerocopy::ByteSlice,
};

macro_rules! validate {
    ( $condition:expr, $debug_message:expr ) => {
        if !$condition {
            return Err($crate::error::FrameParseError::new($debug_message));
        }
    };
}

/// Parse the Wi-Fi Simple Configuration from the probe response
/// This operation performs a copy of the data.
pub fn parse_probe_resp_wsc(raw_body: &[u8]) -> Result<ProbeRespWsc, anyhow::Error> {
    let mut version = None;
    let mut wps_state = None;
    let mut ap_setup_locked = false;
    let mut selected_reg = false;
    let mut selected_reg_config_methods = None;
    let mut response_type = None;
    let mut uuid_e = None;
    let mut manufacturer = None;
    let mut model_name = None;
    let mut model_number = None;
    let mut serial_number = None;
    let mut primary_device_type = None;
    let mut device_name = None;
    let mut config_methods = None;
    let mut rf_bands = None;
    let mut vendor_ext = vec![];

    for (id, body) in Reader::new(raw_body) {
        match id {
            Id::VERSION => version = Some(parse_version(body)?),
            Id::WPS_STATE => wps_state = Some(parse_wps_state(body)?),
            Id::AP_SETUP_LOCKED => ap_setup_locked = parse_bool(body).context("AP setup locked")?,
            Id::SELECTED_REG => selected_reg = parse_bool(body).context("Selected reg")?,
            Id::SELECTED_REG_CONFIG_METHODS => {
                selected_reg_config_methods = Some(parse_selected_reg_config_methods(body)?)
            }
            Id::RESPONSE_TYPE => response_type = Some(parse_response_type(body)?),
            Id::UUID_E => uuid_e = Some(parse_uuid_e(body)?),
            Id::MANUFACTURER => manufacturer = Some(parse_manufacturer(body)?.to_vec()),
            Id::MODEL_NAME => model_name = Some(parse_model_name(body)?.to_vec()),
            Id::MODEL_NUMBER => model_number = Some(parse_model_number(body)?.to_vec()),
            Id::SERIAL_NUMBER => serial_number = Some(parse_serial_number(body)?.to_vec()),
            Id::PRIMARY_DEVICE_TYPE => primary_device_type = Some(parse_primary_device_type(body)?),
            Id::DEVICE_NAME => device_name = Some(parse_device_name(body)?.to_vec()),
            Id::CONFIG_METHODS => config_methods = Some(parse_config_methods(body)?),
            Id::RF_BANDS => rf_bands = Some(parse_rf_bands(body)?),
            Id::VENDOR_EXT => vendor_ext.extend_from_slice(body),
            _ => (),
        }
    }

    let version = version.ok_or(format_err!("Missing version"))?;
    let wps_state = wps_state.ok_or(format_err!("Missing WSC state"))?;
    let response_type = response_type.ok_or(format_err!("Missing response type"))?;
    let uuid_e = uuid_e.ok_or(format_err!("Missing UUID-E"))?;
    let manufacturer = manufacturer.ok_or(format_err!("Missing manufacturer"))?;
    let model_name = model_name.ok_or(format_err!("Missing model name"))?;
    let model_number = model_number.ok_or(format_err!("Missing model number"))?;
    let serial_number = serial_number.ok_or(format_err!("Missing serial number"))?;
    let primary_device_type =
        primary_device_type.ok_or(format_err!("Missing primary device type"))?;
    let device_name = device_name.ok_or(format_err!("Missing device name"))?;
    let config_methods = config_methods.ok_or(format_err!("Missing config methods"))?;

    Ok(ProbeRespWsc {
        version,
        wps_state,
        ap_setup_locked,
        selected_reg,
        selected_reg_config_methods,
        response_type,
        uuid_e,
        manufacturer,
        model_name,
        model_number,
        serial_number,
        primary_device_type,
        device_name,
        config_methods,
        rf_bands,
        vendor_ext,
    })
}

pub fn parse_version(raw_body: &[u8]) -> FrameParseResult<u8> {
    validate!(raw_body.len() == 1, "Version attribute is not the right length");
    Ok(raw_body[0])
}

pub fn parse_wps_state(raw_body: &[u8]) -> FrameParseResult<WpsState> {
    validate!(raw_body.len() == 1, "WPS state attribute is not the right length");
    Ok(WpsState(raw_body[0]))
}

pub fn parse_bool(raw_body: &[u8]) -> FrameParseResult<bool> {
    validate!(raw_body.len() == 1, "Bool attribute is not the right length");
    Ok(raw_body[0] != 0)
}

pub fn parse_selected_reg_config_methods(raw_body: &[u8]) -> FrameParseResult<[u8; 2]> {
    raw_body
        .try_into()
        .map_err(|_| FrameParseError::new("Failed to parse selected registrar config methods"))
}

pub fn parse_response_type(raw_body: &[u8]) -> FrameParseResult<u8> {
    validate!(raw_body.len() == 1, "Response type attribute is not the right length");
    Ok(raw_body[0])
}

pub fn parse_uuid_e(raw_body: &[u8]) -> FrameParseResult<[u8; 16]> {
    raw_body.try_into().map_err(|_| FrameParseError::new("Failed to parse UUID IE"))
}

pub fn parse_manufacturer<B: ByteSlice>(raw_body: B) -> FrameParseResult<B> {
    validate!(raw_body.len() <= MANUFACTURER_ATTR_MAX_LEN, "Manufacturer attribute is too long");
    Ok(raw_body)
}

pub fn parse_model_name<B: ByteSlice>(raw_body: B) -> FrameParseResult<B> {
    validate!(raw_body.len() <= MODEL_NAME_ATTR_MAX_LEN, "Model name attribute is too long");
    Ok(raw_body)
}

pub fn parse_model_number<B: ByteSlice>(raw_body: B) -> FrameParseResult<B> {
    validate!(raw_body.len() <= MODEL_NUMBER_ATTR_MAX_LEN, "Model number attribute is too long");
    Ok(raw_body)
}

pub fn parse_serial_number<B: ByteSlice>(raw_body: B) -> FrameParseResult<B> {
    validate!(raw_body.len() <= SERIAL_NUMBER_ATTR_MAX_LEN, "Serial number attribute is too long");
    Ok(raw_body)
}

pub fn parse_primary_device_type(raw_body: &[u8]) -> FrameParseResult<[u8; 8]> {
    raw_body.try_into().map_err(|_| FrameParseError::new("Failed to parse primary device type"))
}

pub fn parse_device_name<B: ByteSlice>(raw_body: B) -> FrameParseResult<B> {
    validate!(raw_body.len() <= DEVICE_NAME_ATTR_MAX_LEN, "Serial number attribute is too long");
    Ok(raw_body)
}

pub fn parse_config_methods(raw_body: &[u8]) -> FrameParseResult<[u8; 2]> {
    raw_body.try_into().map_err(|_| FrameParseError::new("Failed to parse config methods"))
}

pub fn parse_rf_bands(raw_body: &[u8]) -> FrameParseResult<u8> {
    validate!(raw_body.len() == 1, "RF Bands is not the right length");
    Ok(raw_body[0])
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ie::fake_ies::{fake_probe_resp_wsc_ie, fake_probe_resp_wsc_ie_bytes};

    #[test]
    fn test_parse_probe_resp_wsc() {
        #[rustfmt::skip]
        let raw = fake_probe_resp_wsc_ie_bytes();
        let expected = fake_probe_resp_wsc_ie();

        let parsed = parse_probe_resp_wsc(&raw[..]);
        assert!(parsed.is_ok());
        assert_eq!(parsed.unwrap(), expected);
    }
}
