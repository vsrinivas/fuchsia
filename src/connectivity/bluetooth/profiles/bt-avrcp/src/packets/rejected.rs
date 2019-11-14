// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

#[derive(Debug)]
/// For rejection packets. This packets operates differently from most vendor dependant packets in
/// that there is only a encoder and that the PDU ID can be set to match the PDU ID of the command
/// we rejecting.
/// TODO(2743): Add support for VendorResponse trait for RejectResponse.
/// AVRCP 1.6.1 section 6.15 Error handling
pub struct RejectResponse {
    pub pdu_id: u8,
    pub status_code: u8,
}

impl RejectResponse {
    /// We expect the PDU we are receiving is defined so we take a PduId type instead of U8 because
    /// otherwise we are likely to respond with a NotImplemented instead of a rejection.
    pub fn new(pdu_id: &PduId, status_code: &StatusCode) -> Self {
        Self { pdu_id: u8::from(pdu_id), status_code: u8::from(status_code) }
    }
}

impl VendorDependent for RejectResponse {
    fn pdu_id(&self) -> PduId {
        PduId::try_from(self.pdu_id).unwrap()
    }
}

impl Encodable for RejectResponse {
    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::OutOfRange);
        }

        buf[0] = self.status_code;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_reject_response() {
        let b = RejectResponse::new(&PduId::RegisterNotification, &StatusCode::InvalidParameter);
        assert_eq!(b.pdu_id(), PduId::RegisterNotification);
        assert_eq!(b.encoded_len(), 1);
        let mut buf = vec![0; b.encoded_len()];
        assert!(b.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x01]);

        buf = b.encode_packet().expect("Unable to encode packet");
        assert_eq!(buf.len(), 5);
        assert_eq!(
            buf,
            &[
                0x31, // RegisterNotification pdu id
                0x00, // single packet
                0x00, 0x01, // param len,
                0x01, //payload
            ]
        )
    }
}
