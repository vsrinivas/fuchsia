// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    packet_encoding::{decodable_enum, Decodable, Encodable},
    std::convert::TryFrom,
};

use crate::packets::{
    AvcCommandType, Error, PacketResult, PduId, VendorCommand, VendorDependentPdu,
};

decodable_enum! {
    /// AVRCP 1.6.1 section 6.4.1 Table 6.5
    pub enum GetCapabilitiesCapabilityId <u8, Error, InvalidParameter> {
        CompanyId = 0x02,
        EventsId = 0x03,
    }
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.4 Capabilities PDUs - GetCapabilities
pub struct GetCapabilitiesCommand {
    capability_id: GetCapabilitiesCapabilityId,
}

impl GetCapabilitiesCommand {
    pub fn new(capability_id: GetCapabilitiesCapabilityId) -> GetCapabilitiesCommand {
        GetCapabilitiesCommand { capability_id }
    }

    pub fn capability_id(&self) -> GetCapabilitiesCapabilityId {
        self.capability_id
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for GetCapabilitiesCommand {
    fn pdu_id(&self) -> PduId {
        PduId::GetCapabilities
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for GetCapabilitiesCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Status
    }
}

impl Decodable for GetCapabilitiesCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessageLength);
        }

        let capability_id = GetCapabilitiesCapabilityId::try_from(buf[0])?;

        Ok(Self { capability_id })
    }
}

impl Encodable for GetCapabilitiesCommand {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::BufferLengthOutOfRange);
        }
        buf[0] = u8::from(&self.capability_id);
        Ok(())
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Hash)]
pub enum Capability {
    CompanyId(u8, u8, u8),
    EventId(u8),
}

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.4 Capabilities PDUs - GetCapabilities
pub struct GetCapabilitiesResponse {
    capability_id: GetCapabilitiesCapabilityId,
    capabilities: Vec<Capability>,
}

const BT_SIG_COMPANY_ID: Capability = Capability::CompanyId(0x00, 0x19, 0x58);

impl GetCapabilitiesResponse {
    // Generic BT SIG company ID response
    pub fn new_btsig_company() -> GetCapabilitiesResponse {
        GetCapabilitiesResponse {
            capability_id: GetCapabilitiesCapabilityId::CompanyId,
            capabilities: vec![BT_SIG_COMPANY_ID], // BT_SIG generic company ID
        }
    }

    pub fn new_events(event_ids: &[u8]) -> GetCapabilitiesResponse {
        let capabilities = event_ids.into_iter().map(|id| Capability::EventId(*id)).collect();
        GetCapabilitiesResponse {
            capability_id: GetCapabilitiesCapabilityId::EventsId,
            capabilities,
        }
    }

    #[allow(dead_code)]
    pub fn capability_id(&self) -> GetCapabilitiesCapabilityId {
        self.capability_id
    }

    #[allow(dead_code)]
    pub fn has_bt_sig_company(&self) -> bool {
        assert_eq!(self.capability_id, GetCapabilitiesCapabilityId::CompanyId);

        self.capabilities.contains(&BT_SIG_COMPANY_ID)
    }

    pub fn event_ids(&self) -> Vec<u8> {
        self.capabilities
            .iter()
            .map(|f| match f {
                Capability::EventId(id) => *id,
                _ => panic!("Capabilities contains non events"),
            })
            .collect()
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for GetCapabilitiesResponse {
    fn pdu_id(&self) -> PduId {
        PduId::GetCapabilities
    }
}

impl Decodable for GetCapabilitiesResponse {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 2 {
            return Err(Error::InvalidMessageLength);
        }
        let capability_id = GetCapabilitiesCapabilityId::try_from(buf[0])?;
        let capability_count = buf[1] as usize;
        if capability_count > 0 && buf.len() == 2 {
            return Err(Error::InvalidMessageLength);
        }
        let capabilities = match capability_id {
            GetCapabilitiesCapabilityId::CompanyId => {
                let mut company_ids = vec![];
                let mut chunks = buf[2..].chunks_exact(3);
                while let Some(chunk) = chunks.next() {
                    company_ids.push(Capability::CompanyId(chunk[0], chunk[1], chunk[2]));
                }
                if chunks.remainder().len() > 0 {
                    return Err(Error::InvalidMessage);
                }
                company_ids
            }
            GetCapabilitiesCapabilityId::EventsId => {
                let mut event_ids = vec![];
                for chunk in &buf[2..] {
                    event_ids.push(Capability::EventId(*chunk))
                }
                event_ids
            }
        };

        if capabilities.len() != capability_count {
            return Err(Error::InvalidMessage);
        }

        Ok(Self { capability_id, capabilities })
    }
}

impl Encodable for GetCapabilitiesResponse {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        const PREFIX_LENGTH: usize = 2; // capability_id + count
        match self.capability_id {
            GetCapabilitiesCapabilityId::CompanyId => PREFIX_LENGTH + (self.capabilities.len() * 3),
            GetCapabilitiesCapabilityId::EventsId => PREFIX_LENGTH + (self.capabilities.len() * 1),
        }
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < self.encoded_len() {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = u8::from(&self.capability_id);
        buf[1] =
            u8::try_from(self.capabilities.len()).map_err(|_| Error::ParameterEncodingError)?;
        match self.capability_id {
            GetCapabilitiesCapabilityId::CompanyId => {
                let mut i = 2;
                for capability in self.capabilities.iter() {
                    if let Capability::CompanyId(b1, b2, b3) = capability {
                        buf[i] = *b1;
                        buf[i + 1] = *b2;
                        buf[i + 2] = *b3;
                        i += 3;
                    } else {
                        return Err(Error::ParameterEncodingError);
                    }
                }
            }
            GetCapabilitiesCapabilityId::EventsId => {
                let mut i = 2;
                for capability in self.capabilities.iter() {
                    if let Capability::EventId(b1) = capability {
                        buf[i] = *b1;
                        i += 1;
                    } else {
                        return Err(Error::InvalidParameter);
                    }
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::packets::{NotificationEventId, VendorDependentRawPdu};

    #[test]
    /// Test GetCapabilitiesResponse company encoding
    fn test_get_capabilities_response_company_encode() {
        let b = GetCapabilitiesResponse::new_btsig_company();
        assert!(b.has_bt_sig_company());
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::GetCapabilities));
        assert_eq!(b.capability_id(), GetCapabilitiesCapabilityId::CompanyId);
        assert_eq!(b.encoded_len(), 5); // 1 company + len + capability id
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            &[
                0x02, // company_id
                0x01, // len
                0x00, 0x19, 0x58 // BT_SIG Company ID
            ]
        );
    }

    #[test]
    /// Test GetCapabilitiesResponse company decoding
    fn test_get_capabilities_response_company_decode() {
        assert!(GetCapabilitiesResponse::decode(&[
            0x02, // company_id
            0x02, // len
            0x00, 0x19, 0x58, // BT_SIG Company ID
            0xff, 0xff, 0xff, // random
        ])
        .unwrap()
        .has_bt_sig_company());
    }

    #[test]
    /// Test GetCapabilitiesResponse bad company decoding
    fn test_get_capabilities_response_company_decode_bad() {
        assert!(GetCapabilitiesResponse::decode(&[
            0x02, // company_id
            0x02, // len
            0x00, 0x19,
        ])
        .is_err());
    }

    #[test]
    /// Test GetCapabilitiesResponse decode error too short
    fn test_get_capabilities_response_decode_bad_short() {
        assert!(GetCapabilitiesResponse::decode(&[]).is_err());
    }

    #[test]
    /// Test GetCapabilitiesResponse event encoding (all events)
    fn test_get_capabilities_response_event_encode_all() {
        let b = GetCapabilitiesResponse::new_events(NotificationEventId::VALUES);
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::GetCapabilities));
        assert_eq!(b.capability_id(), GetCapabilitiesCapabilityId::EventsId);
        assert_eq!(b.encoded_len(), 2 + NotificationEventId::VALUES.len()); // all the events + len + capability id
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
    }

    #[test]
    /// Test GetCapabilitiesResponse event encoding (two events)
    fn test_get_capabilities_response_event_encode_two() {
        let b = GetCapabilitiesResponse::new_events(&[0x01, 0x02]);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            &[
                0x03, // event id
                0x02, // len
                0x01, 0x02, // two events
            ]
        );
    }

    #[test]
    /// Test GetCapabilitiesResponse parsing of events
    fn test_get_capabilities_response_event_decode() {
        let resp = GetCapabilitiesResponse::decode(&[
            0x03, // event id
            0x02, // len
            0x01, 0x02, // two events
        ]);
        assert!(resp.is_ok(), "unable to decode {:?}", resp.err());
        assert_eq!(resp.unwrap().event_ids(), &[0x01, 0x02]);
    }

    #[test]
    /// Test GetCapabilitiesResponse bad decode
    fn test_get_capabilities_response_decode_bad() {
        assert!(GetCapabilitiesResponse::decode(&[
            0x01, // bad id
            0xff, // bad len
            0x01, 0x02, // two params
        ])
        .is_err());
    }

    #[test]
    /// Test GetCapabilitiesCommand decode error too short
    fn test_get_capabilities_command_decode_bad_short() {
        assert!(GetCapabilitiesCommand::decode(&[]).is_err());
    }

    #[test]
    /// Test GetCapabilitiesCommand company encoding
    fn test_get_capabilities_command_company_encode() {
        let b = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::CompanyId);
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::GetCapabilities));
        assert_eq!(b.command_type(), AvcCommandType::Status);
        assert_eq!(b.capability_id(), GetCapabilitiesCapabilityId::CompanyId);
        assert_eq!(b.encoded_len(), 1); // capability id
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            &[
                0x02, // company id
            ]
        );
    }

    #[test]
    /// Test GetCapabilitiesCommand event encoding
    fn test_get_capabilities_command_event_encode() {
        let b = GetCapabilitiesCommand::new(GetCapabilitiesCapabilityId::EventsId);
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::GetCapabilities));
        assert_eq!(b.command_type(), AvcCommandType::Status);
        assert_eq!(b.capability_id(), GetCapabilitiesCapabilityId::EventsId);
        assert_eq!(b.encoded_len(), 1); // capability id
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(
            buf,
            &[
                0x03, // event id
            ]
        );
    }

    #[test]
    /// Test GetCapabilitiesResponse parsing of events
    fn test_get_capabilities_command_event_decode() {
        let result = GetCapabilitiesCommand::decode(&[
            0x03, // event id
        ]);
        assert!(result.is_ok());
        assert_eq!(result.unwrap().capability_id(), GetCapabilitiesCapabilityId::EventsId);
    }
}
