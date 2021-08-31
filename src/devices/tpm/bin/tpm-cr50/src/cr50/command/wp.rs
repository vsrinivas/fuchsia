// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    cr50::command::{Deserializable, Header, Serializable, Subcommand, TpmRequest},
    util::{DeserializeError, Deserializer, Serializer},
};
use fidl_fuchsia_tpm_cr50::WpState;

#[derive(Debug, PartialEq)]
/// Write protect response.
pub struct WpResponse {
    header: Header,
    state: u8,
}

impl WpResponse {
    pub fn get_state(&self) -> WpState {
        WpState::from_bits_allow_unknown(self.state)
    }
}

impl Deserializable for WpResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        Ok(WpResponse {
            header: Header::deserialize(deserializer)?,
            state: deserializer.take_u8()?,
        })
    }
}

#[derive(Debug, PartialEq)]
/// Get write protect info from the TPM.
pub struct WpInfoRequest {
    header: Header,
}

impl WpInfoRequest {
    pub fn new() -> Self {
        WpInfoRequest { header: Header::new(Subcommand::Wp) }
    }
}

impl TpmRequest for WpInfoRequest {
    type ResponseType = WpResponse;
}

impl Serializable for WpInfoRequest {
    fn serialize(&self, serializer: &mut Serializer) {
        self.header.serialize(serializer);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_wp_info_request_serialize() {
        let mut serializer = Serializer::new();
        WpInfoRequest::new().serialize(&mut serializer);
        let data = serializer.into_vec();

        assert_eq!(data, vec![0x00, 0x27]);
    }

    #[test]
    fn test_wp_state_deserialize() {
        let response = vec![
            0x00, 0x27, /* header - Subcommand::Wp */
            0x06, /* state: ENABLE | FORCE */
        ];

        let mut deserializer = Deserializer::new(response);
        let res = WpResponse::deserialize(&mut deserializer).unwrap();
        assert_eq!(res.get_state(), WpState::Enable | WpState::Force);
    }
}
