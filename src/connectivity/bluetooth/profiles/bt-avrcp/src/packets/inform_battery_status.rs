// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_avrcp::BatteryStatus;
use packet_encoding::{Decodable, Encodable};

use crate::packets::{
    AvcCommandType, Error, PacketResult, PduId, VendorCommand, VendorDependentPdu,
};

/// AVRCP v1.6.2 Section 6.5.8 InformBatteryStatusOfCT.
#[derive(Debug)]
pub struct InformBatteryStatusOfCtCommand {
    battery_status: BatteryStatus,
}

impl InformBatteryStatusOfCtCommand {
    pub fn new(battery_status: BatteryStatus) -> InformBatteryStatusOfCtCommand {
        Self { battery_status }
    }

    #[cfg(test)]
    pub fn battery_status(&self) -> BatteryStatus {
        self.battery_status
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for InformBatteryStatusOfCtCommand {
    fn pdu_id(&self) -> PduId {
        PduId::InformBatteryStatusOfCT
    }
}

impl VendorCommand for InformBatteryStatusOfCtCommand {
    fn command_type(&self) -> AvcCommandType {
        AvcCommandType::Control
    }
}

impl Decodable for InformBatteryStatusOfCtCommand {
    type Error = Error;

    fn decode(buf: &[u8]) -> PacketResult<Self> {
        if buf.len() < 1 {
            return Err(Error::InvalidMessageLength);
        }

        let battery_status =
            BatteryStatus::from_primitive(buf[0]).ok_or(Error::InvalidParameter)?;

        Ok(Self { battery_status })
    }
}

impl Encodable for InformBatteryStatusOfCtCommand {
    type Error = Error;

    fn encoded_len(&self) -> usize {
        1
    }

    fn encode(&self, buf: &mut [u8]) -> PacketResult<()> {
        if buf.len() < 1 {
            return Err(Error::BufferLengthOutOfRange);
        }

        buf[0] = self.battery_status.into_primitive();
        Ok(())
    }
}

/// AVRCP v1.6.1 Section 6.5.8 InformBatteryStatusOfCT response is empty.
#[derive(Debug)]
pub struct InformBatteryStatusOfCtResponse {}

impl InformBatteryStatusOfCtResponse {
    #[cfg(test)]
    pub fn new() -> InformBatteryStatusOfCtResponse {
        Self {}
    }
}

/// Packet PDU ID for vendor dependent packet encoding.
impl VendorDependentPdu for InformBatteryStatusOfCtResponse {
    fn pdu_id(&self) -> PduId {
        PduId::InformBatteryStatusOfCT
    }
}

impl Decodable for InformBatteryStatusOfCtResponse {
    type Error = Error;

    fn decode(_buf: &[u8]) -> PacketResult<Self> {
        Ok(Self {})
    }
}

impl Encodable for InformBatteryStatusOfCtResponse {
    type Error = Error;

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
    fn inform_battery_status_of_ct_command_encode() {
        let cmd = InformBatteryStatusOfCtCommand::new(BatteryStatus::Warning);
        assert_eq!(cmd.encoded_len(), 1);
        assert_eq!(cmd.command_type(), AvcCommandType::Control);
        assert_eq!(cmd.pdu_id(), PduId::InformBatteryStatusOfCT);
        let mut buf = vec![0; cmd.encoded_len()];
        assert!(cmd.encode(&mut buf[..]).is_ok());
        assert_eq!(buf, &[0x01]);
    }

    #[test]
    fn inform_battery_status_of_ct_command_decode() {
        let cmd = InformBatteryStatusOfCtCommand::decode(&[0x03]).expect("unable to decode packet");
        assert_eq!(cmd.encoded_len(), 1);
        assert_eq!(cmd.battery_status(), BatteryStatus::External);
    }

    #[test]
    fn inform_battery_status_of_ct_command_decode_invalid_battery_level() {
        // Out of range battery level.
        let cmd = InformBatteryStatusOfCtCommand::decode(&[0x10]);
        assert_eq!(cmd.expect_err("expected error"), Error::InvalidParameter);
    }

    #[test]
    fn inform_battery_status_of_ct_command_decode_empty_buffer() {
        // Out of range battery level.
        let cmd = InformBatteryStatusOfCtCommand::decode(&[]);
        assert_eq!(cmd.expect_err("expected error"), Error::InvalidMessageLength);
    }

    #[test]
    fn inform_battery_status_of_ct_response_encode() {
        let response = InformBatteryStatusOfCtResponse::new();
        assert_eq!(response.encoded_len(), 0);
        assert_eq!(response.pdu_id(), PduId::InformBatteryStatusOfCT);
        let mut buf = vec![0; response.encoded_len()];
        assert!(response.encode(&mut buf[..]).is_ok());
        assert_eq!(buf.len(), 0);
    }

    #[test]
    fn inform_battery_status_of_ct_response_decode() {
        let response =
            InformBatteryStatusOfCtResponse::decode(&[]).expect("unable to decode packet");
        assert_eq!(response.encoded_len(), 0);
    }
}
