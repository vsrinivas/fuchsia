// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

#[derive(Debug)]
/// AVRCP 1.6.1 section 6.8 Continuation PDUs - RequestContinuingResponse
pub struct RequestContinuingResponseCommand {
    pdu_id: u8,
}

impl RequestContinuingResponseCommand {
    pub fn new(pdu_id: &PduId) -> Self {
        Self { pdu_id: u8::from(pdu_id) }
    }

    #[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
    pub fn pdu_id_response(&self) -> u8 {
        self.pdu_id
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for RequestContinuingResponseCommand {
    fn pdu_id(&self) -> PduId {
        PduId::RequestContinuingResponse
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for RequestContinuingResponseCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for RequestContinuingResponseCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessageLength);
        }

        Ok(Self { pdu_id: buf[0] })
    }
}

impl Encodable for RequestContinuingResponseCommand {
    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = self.pdu_id;
        Ok(())
    }
}

#[derive(Debug)]
#[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
/// AVRCP 1.6.1 section 6.8 Continuation PDUs - AbortContinuingResponse
pub struct AbortContinuingResponseCommand {
    pdu_id: u8,
}

impl AbortContinuingResponseCommand {
    #[allow(dead_code)]
    pub fn new(pdu_id: &PduId) -> Self {
        Self { pdu_id: u8::from(pdu_id) }
    }

    #[allow(dead_code)]
    pub fn pdu_id_response(&self) -> u8 {
        self.pdu_id
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for AbortContinuingResponseCommand {
    fn pdu_id(&self) -> PduId {
        PduId::AbortContinuingResponse
    }
}

/// Specifies the AVC command type for this AVC command packet
impl VendorCommand for AbortContinuingResponseCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for AbortContinuingResponseCommand {
    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessageLength);
        }

        Ok(Self { pdu_id: buf[0] })
    }
}

impl Encodable for AbortContinuingResponseCommand {
    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = self.pdu_id;
        Ok(())
    }
}

#[derive(Debug)]
#[allow(dead_code)] // TODO(fxbug.dev/2741): WIP. Remove once used.
/// AVRCP 1.6.1 section 6.8 Continuation PDUs - AbortContinuingResponse
pub struct AbortContinuingResponseResponse {}

impl AbortContinuingResponseResponse {
    #[allow(dead_code)]
    pub fn new() -> Self {
        Self {}
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for AbortContinuingResponseResponse {
    fn pdu_id(&self) -> PduId {
        PduId::AbortContinuingResponse
    }
}

impl Decodable for AbortContinuingResponseResponse {
    fn decode(_buf: &[u8]) -> PacketResult<Self> {
        Ok(Self {})
    }
}

impl Encodable for AbortContinuingResponseResponse {
    fn encoded_len(&self) -> usize {
        0
    }

    fn encode(&self, _buf: &mut [u8]) -> PacketResult<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_request_continuing_response_encode() {
        let b = RequestContinuingResponseCommand::new(&PduId::GetCapabilities);
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::RequestContinuingResponse));
        assert_eq!(b.command_type(), AvcCommandType::Control);
        assert_eq!(b.encoded_len(), 1);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x10]);

        buf = b.encode_packet().expect("Unable to encode packet");
        assert_eq!(buf.len(), 5);
        assert_eq!(
            buf,
            &[
                0x40, // RequestContinuingResponse pdu id
                0x00, // single packet
                0x00, 0x01, // param len,
                0x10, //payload
            ]
        )
    }

    #[test]
    fn test_request_continuing_response_decode() {
        let b = RequestContinuingResponseCommand::decode(&[
            0x10, //payload
        ])
        .expect("unable to parse packet");
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::RequestContinuingResponse));
        assert_eq!(b.pdu_id_response(), 0x10);
        assert_eq!(b.encoded_len(), 1);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x10]);
    }

    #[test]
    fn test_abort_continuing_response_command_encode() {
        let b = AbortContinuingResponseCommand::new(&PduId::GetCapabilities);
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::AbortContinuingResponse));
        assert_eq!(b.command_type(), AvcCommandType::Control);
        assert_eq!(b.encoded_len(), 1);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x10]);

        buf = b.encode_packet().expect("Unable to encode packet");
        assert_eq!(buf.len(), 5);
        assert_eq!(
            buf,
            &[
                0x41, // AbortContinuingResponse pdu id
                0x00, // single packet
                0x00, 0x01, // param len,
                0x10, //payload
            ]
        )
    }

    #[test]
    fn test_abort_continuing_response_command_decode() {
        let b = AbortContinuingResponseCommand::decode(&[
            0x10, //payload
        ])
        .expect("unable to parse packet");
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::AbortContinuingResponse));
        assert_eq!(b.pdu_id_response(), 0x10);
        assert_eq!(b.encoded_len(), 1);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x10]);
    }

    #[test]
    fn test_abort_continuing_response_response_encode() {
        let b = AbortContinuingResponseResponse::new();
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::AbortContinuingResponse));
        assert_eq!(b.encoded_len(), 0);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf.len(), 0);

        buf = b.encode_packet().expect("Unable to encode packet");
        assert_eq!(buf.len(), 4);
        assert_eq!(
            buf,
            &[
                0x41, // AbortContinuingResponse pdu id
                0x00, // single packet
                0x00, 0x00, // param len
            ]
        )
    }

    #[test]
    fn test_abort_continuing_response_response_decode() {
        let b = AbortContinuingResponseResponse::decode(&[]).expect("unable to parse packet");
        assert_eq!(b.raw_pdu_id(), u8::from(&PduId::AbortContinuingResponse));
        assert_eq!(b.encoded_len(), 0);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf.len(), 0);
    }
}
