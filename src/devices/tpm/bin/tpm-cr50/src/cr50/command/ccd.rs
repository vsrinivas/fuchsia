// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    cr50::command::{Deserializable, Header, Serializable, Subcommand, TpmRequest},
    util::{DeserializeError, Deserializer, Serializer},
};
use fidl_fuchsia_tpm_cr50::{
    CcdCapability, CcdCapabilitySetting, CcdCapabilityState, PhysicalPresenceState,
};
use num_derive::FromPrimitive;
use std::{
    ffi::{CString, NulError},
    marker::PhantomData,
};

#[repr(u8)]
#[derive(FromPrimitive, Debug, Copy, Clone, PartialEq, Eq)]
/// Case-closed debugging commands.
pub enum CcdCommand {
    Password = 0,
    Open = 1,
    Unlock = 2,
    Lock = 3,
    CmdPpPollUnlock = 4,
    CmdPpPollOpen = 5,
    GetInfo = 6,
}

/// A case-closed debugging request. |T| determines the response type.
pub struct CcdRequest<T> {
    header: Header,
    cmd: u8,
    password: Option<CString>,
    _response: PhantomData<T>,
}

impl<T> CcdRequest<T> {
    pub fn new(cmd: CcdCommand) -> Self {
        CcdRequest::<T> {
            header: Header::new(Subcommand::Ccd),
            cmd: cmd as u8,
            password: None,
            _response: PhantomData,
        }
    }

    #[allow(dead_code)] // TODO(fxbug.dev/82504): remove when passwords are supported.
    pub fn new_with_password(cmd: CcdCommand, password: &str) -> Result<Self, NulError> {
        Ok(CcdRequest::<T> {
            header: Header::new(Subcommand::Ccd),
            cmd: cmd as u8,
            password: Some(CString::new(password)?),
            _response: PhantomData,
        })
    }
}

impl<T: Deserializable> TpmRequest for CcdRequest<T> {
    type ResponseType = T;
}

impl<T> Serializable for CcdRequest<T> {
    fn serialize(&self, serializer: &mut Serializer) {
        self.header.serialize(serializer);
        serializer.put_u8(self.cmd);
        if let Some(password) = &self.password {
            serializer.put(password.as_bytes_with_nul());
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
#[repr(C, packed)]
/// Response to CcdCommand::GetInfo.
pub struct CcdGetInfoResponse {
    header: Header,
    ccd_caps_current: [u32; 2],
    ccd_caps_defaults: [u32; 2],
    pub ccd_flags: u32,
    pub ccd_state: u8,
    pub ccd_forced_disabled: u8,
    pub ccd_indicator_bitmap: u8,
}

impl CcdGetInfoResponse {
    /// Get the |CcdCapabilitySetting| representing |which|.
    pub fn get_capability(&self, which: CcdCapability) -> CcdCapabilitySetting {
        let cap_num = which.into_primitive();
        let index = ((cap_num * 2) / 32) as usize;
        let offset = ((cap_num * 2) % 32) as usize;

        let current = (self.ccd_caps_current[index] >> offset) & 3;
        let default = (self.ccd_caps_defaults[index] >> offset) & 3;

        CcdCapabilitySetting {
            capability: which,
            current_state: CcdCapabilityState::from_primitive(current).unwrap(),
            default_state: CcdCapabilityState::from_primitive(default).unwrap(),
        }
    }
}

impl Deserializable for CcdGetInfoResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let header = Header::deserialize(deserializer)?;
        let result = CcdGetInfoResponse {
            header,
            ccd_caps_current: [deserializer.take_be_u32()?, deserializer.take_be_u32()?],
            ccd_caps_defaults: [deserializer.take_be_u32()?, deserializer.take_be_u32()?],
            ccd_flags: deserializer.take_be_u32()?,
            ccd_state: deserializer.take_u8()?,
            ccd_forced_disabled: deserializer.take_u8()?,
            ccd_indicator_bitmap: deserializer.take_u8()?,
        };

        Ok(result)
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
#[repr(C, packed)]
/// Response to CmdPpPoll* commands.
pub struct CcdPhysicalPresenceResponse {
    header: Header,
    state: u8,
}

impl Deserializable for CcdPhysicalPresenceResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let header = Header::deserialize(deserializer)?;
        Ok(CcdPhysicalPresenceResponse { header, state: deserializer.take_u8()? })
    }
}

impl CcdPhysicalPresenceResponse {
    pub fn get_state(&self) -> PhysicalPresenceState {
        PhysicalPresenceState::from_primitive_allow_unknown(self.state.into())
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct CcdOpenResponse {
    header: Header,
    ec_rc: Option<u8>,
}

impl Deserializable for CcdOpenResponse {
    fn deserialize(deserializer: &mut Deserializer) -> Result<Self, DeserializeError> {
        let header = Header::deserialize(deserializer)?;
        let ec_rc =
            if deserializer.available() >= 1 { Some(deserializer.take_u8()?) } else { None };
        Ok(CcdOpenResponse { header, ec_rc })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::convert::TryInto;

    #[test]
    fn test_ccd_request_serialize() {
        let ccd = CcdRequest::<CcdGetInfoResponse>::new(CcdCommand::Open);
        let mut serializer = Serializer::new();
        ccd.serialize(&mut serializer);

        let state = serializer.into_vec();
        assert_eq!(state.len(), 3);
        assert_eq!(state[2], CcdCommand::Open as u8);
        assert_eq!(u16::from_be_bytes(state[0..2].try_into().unwrap()), Subcommand::Ccd as u16);
    }

    #[test]
    fn test_ccd_request_serialize_with_password() {
        let ccd =
            CcdRequest::<CcdGetInfoResponse>::new_with_password(CcdCommand::Open, "test").unwrap();
        let mut serializer = Serializer::new();
        ccd.serialize(&mut serializer);

        let state = serializer.into_vec();
        assert_eq!(state.len(), 8);
        assert_eq!(state[2], CcdCommand::Open as u8);
        assert_eq!(u16::from_be_bytes(state[0..2].try_into().unwrap()), Subcommand::Ccd as u16);
        assert_eq!(&state[3..], b"test\0");
    }

    #[test]
    fn test_ccd_get_info_response_deserialize() {
        let info = vec![
            0x00, 0x22, /* header - Subcommand::Ccd */
            0x55, 0x55, 0x55, 0x55, /* ccd_caps_current[0] - all ALWAYS */
            0xff, 0xff, 0xff, 0xff, /* ccd_caps_current[1] - all IF_OPENED */
            0xaa, 0xaa, 0xaa, 0xaa, /* ccd_caps_defaults[0] - all UNLESS_LOCKED */
            0x55, 0x55, 0xff, 0xff, /* ccd_caps_defaults[1] - split */
            0x00, 0x08, 0x00, 0x01, /* ccd_flags: TEST_LAB | OVERRIDE_WP_STATE_ENABLED */
            0x00, /* ccd_state: LOCKED */
            0x01, /* ccd_forced_disabled: true */
            0x01, /* ccd_indicator_bitmap: HAS_PASSWORD */
        ];

        let mut response = Deserializer::new(info);
        assert_eq!(
            CcdGetInfoResponse::deserialize(&mut response).unwrap(),
            CcdGetInfoResponse {
                header: Header::new(Subcommand::Ccd),
                ccd_caps_current: [0x55555555, 0xffffffff],
                ccd_caps_defaults: [0xaaaaaaaa, 0x5555ffff],
                ccd_flags: 0x80001,
                ccd_state: 0,
                ccd_forced_disabled: 1,
                ccd_indicator_bitmap: 1,
            }
        );
    }

    #[test]
    fn test_ccd_poll_pp_deserialize() {
        let vec = vec![0x00, 0x22, 0x1];
        let mut response = Deserializer::new(vec);
        assert_eq!(
            CcdPhysicalPresenceResponse::deserialize(&mut response).unwrap().get_state(),
            PhysicalPresenceState::AwaitingPress
        );
    }
}
