// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//trace_macros!(true);

#![allow(dead_code)]

use byteorder::{LittleEndian, WriteBytesExt};
use std::io;

// IEEE Std 802.11-2016, 9.2.4.1.3 Table 9-1
#[derive(Clone, Copy, Debug)]
pub enum FrameControlType {
    Mgmt = 0b00,
}

#[derive(Clone, Copy, Debug)]
pub enum MgmtSubtype {
    Beacon = 0b1000,
}

bitfield!{
    #[derive(Clone, Copy, Debug)]
    pub struct FrameControl(u16);
    pub protocol_ver, set_protocol_ver: 1, 0;
    pub typ, set_typ: 3, 2;
    pub subtype, set_subtype: 7, 4;
    pub to_ds, set_to_ds: 8;
    pub from_ds, set_from_ds: 9;
    pub more_frags, set_more_frags: 10;
    pub retry, set_retry: 11;
    pub power_mgmt, set_power_mgmt: 12;
    pub more_data, set_more_data: 13;
    pub protected_frame, set_protected_frame: 14;
    pub htc_order, set_htc_order: 15;
}

// IEEE Std 802.11-2016, 9.2.4.4
#[derive(Clone, Copy, Debug)]
pub struct SeqControl {
    pub frag_num: u16,
    pub seq_num: u16,
}

impl SeqControl {
    fn encode(&self) -> u16 {
        self.frag_num | (self.seq_num << 4)
    }
}

// IEEE Std 802.11-2016, 9.3.3.2
#[derive(Clone, Copy, Debug)]
pub struct MgmtHeader {
    pub frame_control: FrameControl,
    pub duration: u16,
    pub addr1: [u8; 6],
    pub addr2: [u8; 6],
    pub addr3: [u8; 6],
    pub seq_control: SeqControl,
    pub ht_control: Option<u32>,
}

// IEEE Std 802.11-2016, 9.3.3.3
#[derive(Clone, Copy, Debug)]
pub struct BeaconFields {
    pub timestamp: u64,       // IEEE Std 802.11-2016, 9.4.1.10
    pub beacon_interval: u16, // IEEE Std 802.11-2016, 9.4.1.3
    pub capability_info: u16, // IEEE Std 802.11-2016, 9.4.1.4
}

pub const BROADCAST_ADDR: [u8; 6] = [0xff, 0xff, 0xff, 0xff, 0xff, 0xff];

// IEEE Std 802.11-2016, 9.4.2.1
enum ElementId {
    Ssid = 0,
    SupportedRates = 1,
    DsssParameterSet = 3,
}

pub struct MacFrameWriter<W: io::Write> {
    w: W,
}

impl<W: io::Write> MacFrameWriter<W> {
    pub fn new(w: W) -> Self {
        MacFrameWriter { w }
    }

    pub fn beacon(
        mut self, header: &MgmtHeader, beacon: &BeaconFields,
    ) -> io::Result<ElementWriter<W>> {
        self.write_mgmt_header(header, MgmtSubtype::Beacon)?;
        self.w.write_u64::<LittleEndian>(beacon.timestamp)?;
        self.w.write_u16::<LittleEndian>(beacon.beacon_interval)?;
        self.w.write_u16::<LittleEndian>(beacon.capability_info)?;
        Ok(ElementWriter { w: self.w })
    }

    fn write_mgmt_header(&mut self, header: &MgmtHeader, subtype: MgmtSubtype) -> io::Result<()> {
        let mut frame_control = header.frame_control;
        frame_control.set_typ(FrameControlType::Mgmt as u16);
        frame_control.set_subtype(subtype as u16);
        frame_control.set_htc_order(header.ht_control.is_some());
        self.w.write_u16::<LittleEndian>(frame_control.0)?;
        self.w.write_u16::<LittleEndian>(header.duration)?;
        self.w.write_all(&header.addr1)?;
        self.w.write_all(&header.addr2)?;
        self.w.write_all(&header.addr3)?;
        self.w
            .write_u16::<LittleEndian>(header.seq_control.encode())?;
        if let Some(ht_control) = header.ht_control {
            self.w.write_u32::<LittleEndian>(ht_control)?;
        }
        Ok(())
    }
}

pub struct ElementWriter<W: io::Write> {
    w: W,
}

impl<W: io::Write> ElementWriter<W> {
    pub fn ssid(mut self, ssid: &[u8]) -> io::Result<Self> {
        self.write_header(ElementId::Ssid, ssid.len() as u8)?;
        self.w.write_all(ssid)?;
        Ok(self)
    }

    pub fn supported_rates(mut self, rates: &[u8]) -> io::Result<Self> {
        self.write_header(ElementId::SupportedRates, rates.len() as u8)?;
        self.w.write_all(rates)?;
        Ok(self)
    }

    pub fn dsss_parameter_set(mut self, current_channel: u8) -> io::Result<Self> {
        self.write_header(ElementId::DsssParameterSet, 1)?;
        self.w.write_u8(current_channel)?;
        Ok(self)
    }

    pub fn into_writer(self) -> W {
        self.w
    }

    fn write_header(&mut self, element_id: ElementId, length: u8) -> io::Result<()> {
        self.w.write_u8(element_id as u8)?;
        self.w.write_u8(length)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn simple_beacon() {
        let frame = MacFrameWriter::new(vec![])
            .beacon(
                &MgmtHeader {
                    frame_control: FrameControl(0),
                    duration: 0x9765,
                    addr1: [0x01, 0x02, 0x03, 0x04, 0x05, 0x06],
                    addr2: [0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C],
                    addr3: [0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12],
                    seq_control: SeqControl {
                        frag_num: 5,
                        seq_num: 0xABC,
                    },
                    ht_control: None,
                },
                &BeaconFields {
                    timestamp: 0x1122334455667788u64,
                    beacon_interval: 0xBEAC,
                    capability_info: 0xCAFE,
                },
            )
            .unwrap()
            .ssid(&[0xAA, 0xBB, 0xCC, 0xDD, 0xEE])
            .unwrap()
            .into_writer();
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let expected_frame: &[u8] = &[
            // Framectl Duration    Address 1
            0x80, 0x00, 0x65, 0x97, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // Address 2                        Address 3
            0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
            // Seq ctl  Timestamp
            0xC5, 0xAB, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
            // Interval Cap info    SSID element
            0xAC, 0xBE, 0xFE, 0xCA, 0x00, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE
        ];
        assert_eq!(expected_frame, &frame[..]);
    }
}
