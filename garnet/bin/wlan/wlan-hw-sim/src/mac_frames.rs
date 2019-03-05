// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use byteorder::{LittleEndian, WriteBytesExt};
use std::io;

#[cfg(test)]
use byteorder::ReadBytesExt;

// IEEE Std 802.11-2016, 9.2.4.1.3 Table 9-1
#[derive(Clone, Copy, Debug)]
pub enum FrameControlType {
    Mgmt = 0b00,
    #[cfg(test)]
    Data = 0b10,
}

#[derive(Clone, Copy, Debug)]
pub enum MgmtSubtype {
    AssociationRequest = 0b0000,
    AssociationResponse = 0b0001,
    Beacon = 0b1000,
    Authentication = 0b1011,
    #[cfg(test)]
    Action = 0b1101,
}

#[cfg(test)]
#[derive(Clone, Copy, Debug)]
pub enum DataSubtype {
    Data = 0b0000,
}

// IEEE Std 802.11-2016, 9.4.1.9, Table 9-46
#[derive(Clone, Copy, Debug)]
pub enum StatusCode {
    Success = 0,
}

// IEEE Std 802.11-2016, 9.4.1.1
#[derive(Clone, Copy, Debug)]
pub enum AuthAlgorithm {
    OpenSystem = 0,
}

bitfield! {
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

// IEEE Std 802.11-2016, 9.4.1.4
bitfield! {
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub struct CapabilityInfo(u16);
    pub ess, set_ess: 0;
    pub ibss, set_ibss: 1;
    pub cf_pollable, set_cf_pollable: 2;
    pub cf_pll_req, set_cf_poll_req: 3;
    pub privacy, set_privacy: 4;
    pub short_preamble, set_short_preamble: 5;
    // bit 6-7 reserved
    pub spectrum_mgmt, set_spectrum_mgmt: 8;
    pub qos, set_qos: 9;
    pub short_slot_time, set_short_slot_time: 10;
    pub apsd, set_apsd: 11;
    pub radio_msmt, set_radio_msmt: 12;
    // bit 13 reserved
    pub delayed_block_ack, set_delayed_block_ack: 14;
    pub immediate_block_ack, set_immediate_block_ack: 15;
}

// IEEE Std 802.11-2016, 9.2.4.4
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SeqControl {
    pub frag_num: u16,
    pub seq_num: u16,
}

impl SeqControl {
    #[cfg(test)]
    fn decode(num: u16) -> Self {
        Self { frag_num: num & 0x0F, seq_num: num >> 4 }
    }
    fn encode(&self) -> u16 {
        self.frag_num | (self.seq_num << 4)
    }
}

// IEEE Std 802.11-2016, 9.3.2.1
#[derive(Clone, Copy, Debug)]
pub struct DataHeader {
    pub frame_control: FrameControl,
    pub duration: u16,
    pub addr1: [u8; 6],
    pub addr2: [u8; 6],
    pub addr3: [u8; 6],
    pub seq_control: SeqControl,
    pub addr4: Option<[u8; 6]>,
    pub qos_control: Option<u16>,
    pub ht_control: Option<u32>,
}

#[cfg(test)]
impl DataHeader {
    pub fn from_reader<R: io::Read>(reader: &mut R) -> io::Result<Self> {
        const DATA_SUBTYPE_QOS_BIT: u16 = 0b1000;
        let frame_control = FrameControl(reader.read_u16::<LittleEndian>()?);
        let duration = reader.read_u16::<LittleEndian>()?;
        let mut addr1 = [0u8; 6];
        reader.read(&mut addr1)?;
        let mut addr2 = [0u8; 6];
        reader.read(&mut addr2)?;
        let mut addr3 = [0u8; 6];
        reader.read(&mut addr3)?;
        let seq_control = SeqControl::decode(reader.read_u16::<LittleEndian>()?);
        let addr4 = match frame_control.to_ds() & frame_control.from_ds() {
            true => {
                let mut addr4 = [0u8; 6];
                reader.read(&mut addr4)?;
                Some(addr4)
            }
            false => None,
        };
        let qos_control = if (frame_control.subtype() & DATA_SUBTYPE_QOS_BIT) != 0 {
            Some(reader.read_u16::<LittleEndian>()?)
        } else {
            None
        };
        let ht_control =
            if frame_control.htc_order() { Some(reader.read_u32::<LittleEndian>()?) } else { None };
        Ok(Self {
            frame_control,
            duration,
            addr1,
            addr2,
            addr3,
            seq_control,
            addr4,
            qos_control,
            ht_control,
        })
    }
}

// IEEE Std 802.2-1998, 3.2
// IETF RFC 1042
#[derive(Clone, Copy, Debug)]
pub struct LlcHeader {
    pub dsap: u8,
    pub ssap: u8,
    pub control: u8,
    pub oui: [u8; 3],
    pub protocol_id: u16,
}

#[cfg(test)]
impl LlcHeader {
    pub fn from_reader<R: io::Read>(reader: &mut R) -> io::Result<Self> {
        let dsap = reader.read_u8()?;
        let ssap = reader.read_u8()?;
        let control = reader.read_u8()?;
        let mut oui = [0u8; 3];
        reader.read(&mut oui)?;
        let protocol_id = reader.read_u16::<LittleEndian>()?;
        Ok(Self { dsap, ssap, control, oui, protocol_id })
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

#[cfg(test)]
impl MgmtHeader {
    pub fn from_reader<R: io::Read>(reader: &mut R) -> io::Result<Self> {
        let frame_control = FrameControl(reader.read_u16::<LittleEndian>()?);
        let duration = reader.read_u16::<LittleEndian>()?;
        let mut addr1 = [0u8; 6];
        reader.read(&mut addr1)?;
        let mut addr2 = [0u8; 6];
        reader.read(&mut addr2)?;
        let mut addr3 = [0u8; 6];
        reader.read(&mut addr3)?;
        let seq_control = SeqControl::decode(reader.read_u16::<LittleEndian>()?);
        let ht_control =
            if frame_control.htc_order() { Some(reader.read_u32::<LittleEndian>()?) } else { None };
        Ok(Self { frame_control, duration, addr1, addr2, addr3, seq_control, ht_control })
    }
}

// IEEE Std 802.11-2016, 9.3.3.3
#[derive(Clone, Copy, Debug)]
pub struct BeaconFields {
    pub timestamp: u64,                  // IEEE Std 802.11-2016, 9.4.1.10
    pub beacon_interval: u16,            // IEEE Std 802.11-2016, 9.4.1.3
    pub capability_info: CapabilityInfo, // IEEE Std 802.11-2016, 9.4.1.4
}

// IEEE Std 802.11-2016, 9.3.3.12
pub struct AuthenticationFields {
    pub auth_algorithm_number: u16, // 9.4.1.1
    pub auth_txn_seq_number: u16,   // 9.4.1.2
    pub status_code: u16,           // 9.4.1.9
}

#[cfg(test)]
impl AuthenticationFields {
    pub fn from_reader<R: io::Read>(reader: &mut R) -> io::Result<Self> {
        let auth_algorithm_number = reader.read_u16::<LittleEndian>()?;
        let auth_txn_seq_number = reader.read_u16::<LittleEndian>()?;
        let status_code = reader.read_u16::<LittleEndian>()?;
        Ok(Self { auth_algorithm_number, auth_txn_seq_number, status_code })
    }
}

// IEEE Std 802.11-2016, 9.3.3.7
pub struct AssociationResponseFields {
    pub capability_info: CapabilityInfo, // 9.4.1.4
    pub status_code: u16,                // 9.4.1.9
    pub association_id: u16,             // 9.4.1.8
}

#[cfg(test)]
impl AssociationResponseFields {
    pub fn from_reader<R: io::Read>(reader: &mut R) -> io::Result<Self> {
        let capability_info = CapabilityInfo(reader.read_u16::<LittleEndian>()?);
        let status_code = reader.read_u16::<LittleEndian>()?;
        let association_id = reader.read_u16::<LittleEndian>()?;
        Ok(Self { capability_info, status_code, association_id })
    }
}

pub const BROADCAST_ADDR: [u8; 6] = [0xff, 0xff, 0xff, 0xff, 0xff, 0xff];

// IEEE Std 802.11-2016, 9.4.2.1
enum ElementId {
    Ssid = 0,
    SupportedRates = 1,
    DsssParameterSet = 3,
    ExtendedSupportedRates = 50,
}

pub struct MacFrameWriter<W: io::Write> {
    w: W,
}

impl<W: io::Write> MacFrameWriter<W> {
    pub fn new(w: W) -> Self {
        MacFrameWriter { w }
    }

    pub fn beacon(
        mut self,
        header: &MgmtHeader,
        beacon: &BeaconFields,
    ) -> io::Result<ElementWriter<W>> {
        self.write_mgmt_header(header, MgmtSubtype::Beacon)?;
        self.w.write_u64::<LittleEndian>(beacon.timestamp)?;
        self.w.write_u16::<LittleEndian>(beacon.beacon_interval)?;
        self.w.write_u16::<LittleEndian>(beacon.capability_info.0 as u16)?;
        Ok(ElementWriter { w: self.w })
    }

    pub fn authentication(
        mut self,
        header: &MgmtHeader,
        auth: &AuthenticationFields,
    ) -> io::Result<ElementWriter<W>> {
        self.write_mgmt_header(header, MgmtSubtype::Authentication)?;
        self.w.write_u16::<LittleEndian>(auth.auth_algorithm_number)?;
        self.w.write_u16::<LittleEndian>(auth.auth_txn_seq_number)?;
        self.w.write_u16::<LittleEndian>(auth.status_code)?;
        Ok(ElementWriter { w: self.w })
    }

    pub fn association_response(
        mut self,
        header: &MgmtHeader,
        assoc: &AssociationResponseFields,
    ) -> io::Result<ElementWriter<W>> {
        self.write_mgmt_header(header, MgmtSubtype::AssociationResponse)?;
        self.w.write_u16::<LittleEndian>(assoc.capability_info.0 as u16)?;
        self.w.write_u16::<LittleEndian>(assoc.status_code)?;
        self.w.write_u16::<LittleEndian>(assoc.association_id)?;
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
        self.w.write_u16::<LittleEndian>(header.seq_control.encode())?;
        if let Some(ht_control) = header.ht_control {
            self.w.write_u32::<LittleEndian>(ht_control)?;
        }
        Ok(())
    }

    #[cfg(test)]
    pub fn data(
        mut self,
        data_header: &DataHeader,
        llc_header: &LlcHeader,
        payload: &[u8],
    ) -> io::Result<Self> {
        self.write_data_header(data_header, DataSubtype::Data)?;
        self.write_llc_header(llc_header)?;
        self.w.write_all(payload)?;
        Ok(self)
    }

    #[cfg(test)]
    fn write_data_header(&mut self, header: &DataHeader, subtype: DataSubtype) -> io::Result<()> {
        let mut frame_control = header.frame_control;
        frame_control.set_typ(FrameControlType::Data as u16);
        frame_control.set_subtype(subtype as u16);
        frame_control.set_htc_order(header.ht_control.is_some());
        frame_control.set_to_ds(false);
        frame_control.set_from_ds(true);
        self.w.write_u16::<LittleEndian>(frame_control.0)?;
        self.w.write_u16::<LittleEndian>(header.duration)?;
        self.w.write_all(&header.addr1)?;
        self.w.write_all(&header.addr2)?;
        self.w.write_all(&header.addr3)?;
        self.w.write_u16::<LittleEndian>(header.seq_control.encode())?;
        if let Some(addr4) = header.addr4 {
            self.w.write_all(&addr4)?;
        }
        if let Some(qos_control) = header.qos_control {
            self.w.write_u16::<LittleEndian>(qos_control)?;
        };
        if let Some(ht_control) = header.ht_control {
            self.w.write_u32::<LittleEndian>(ht_control)?;
        };
        Ok(())
    }

    #[cfg(test)]
    fn write_llc_header(&mut self, header: &LlcHeader) -> io::Result<()> {
        self.w.write_u8(header.dsap)?;
        self.w.write_u8(header.ssap)?;
        self.w.write_u8(header.control)?;
        self.w.write_all(&header.oui)?;
        self.w.write_u16::<LittleEndian>(header.protocol_id)?;
        Ok(())
    }

    #[cfg(test)]
    pub fn into_writer(self) -> W {
        self.w
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

    pub fn extended_supported_rates(mut self, rates: &[u8]) -> io::Result<Self> {
        self.write_header(ElementId::ExtendedSupportedRates, rates.len() as u8)?;
        self.w.write_all(rates)?;
        Ok(self)
    }
    #[cfg(test)]
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
                    seq_control: SeqControl { frag_num: 5, seq_num: 0xABC },
                    ht_control: None,
                },
                &BeaconFields {
                    timestamp: 0x1122334455667788u64,
                    beacon_interval: 0xBEAC,
                    capability_info: CapabilityInfo(0xCAFE),
                },
            )
            .unwrap()
            .ssid(&[0xAA, 0xBB, 0xCC, 0xDD, 0xEE])
            .unwrap()
            .into_writer();
        #[rustfmt::skip]
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

    #[test]
    fn parse_auth_frame() {
        #[rustfmt::skip]
        let mut bytes: &[u8] = &[
            // Framectl Duration    Address 1
            0xb0, 0x00, 0x76, 0x98, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // Address 2                        Address 3
            0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
            // Seq ctl  Algo num    Seq num     Status
            0xC5, 0xAB, 0xB2, 0xA1, 0xAB, 0x23, 0xD4, 0xC3,
        ];
        let hdr = MgmtHeader::from_reader(&mut bytes).expect("reading mgmt header");
        assert_eq!(hdr.frame_control.typ(), FrameControlType::Mgmt as u16);
        assert_eq!(hdr.frame_control.subtype(), MgmtSubtype::Authentication as u16);
        assert_eq!(hdr.duration, 0x9876);
        assert_eq!(hdr.addr1, [0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);
        assert_eq!(hdr.addr2, [0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C]);
        assert_eq!(hdr.addr3, [0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12]);
        assert_eq!(hdr.seq_control, SeqControl { frag_num: 5, seq_num: 0xABC });
        assert_eq!(hdr.ht_control, None);

        let body = AuthenticationFields::from_reader(&mut bytes).expect("reading auth fields");
        assert_eq!(body.auth_algorithm_number, 0xA1B2);
        assert_eq!(body.auth_txn_seq_number, 0x23AB);
        assert_eq!(body.status_code, 0xC3D4);
    }

    #[test]
    fn simple_auth() {
        let frame = MacFrameWriter::new(vec![])
            .authentication(
                &MgmtHeader {
                    frame_control: FrameControl(0),
                    duration: 0x9876,
                    addr1: [0x01, 0x02, 0x03, 0x04, 0x05, 0x06],
                    addr2: [0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C],
                    addr3: [0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12],
                    seq_control: SeqControl { frag_num: 5, seq_num: 0xABC },
                    ht_control: None,
                },
                &AuthenticationFields {
                    auth_algorithm_number: 0xA1B2,
                    auth_txn_seq_number: 0x23AB,
                    status_code: 0xC3D4,
                },
            )
            .unwrap()
            .into_writer();
        #[rustfmt::skip]
        let expected_frame: &[u8] = &[
            // Framectl Duration    Address 1
            0xb0, 0x00, 0x76, 0x98, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // Address 2                        Address 3
            0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
            // Seq ctl  Algo num    Seq num     Status
            0xC5, 0xAB, 0xB2, 0xA1, 0xAB, 0x23, 0xD4, 0xC3,
        ];
        assert_eq!(expected_frame, &frame[..]);
    }

    #[test]
    fn parse_assoc_frame() {
        let mut bytes: &[u8] = &[
            // Framectl Duration    Address 1
            0x10, 0x00, 0x65, 0x87, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // Address 2                        Address 3
            0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
            // Seq ctl  Cap info    Status code Assoc id
            0xF6, 0xDE, 0xDE, 0xBC, 0x76, 0x98, 0x43, 0x65,
        ];
        let hdr = MgmtHeader::from_reader(&mut bytes).expect("reading mgmt header");
        assert_eq!(hdr.frame_control.typ(), FrameControlType::Mgmt as u16);
        assert_eq!(hdr.frame_control.subtype(), MgmtSubtype::AssociationResponse as u16);
        assert_eq!(hdr.duration, 0x8765);
        assert_eq!(hdr.addr1, [0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);
        assert_eq!(hdr.addr2, [0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C]);
        assert_eq!(hdr.addr3, [0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12]);
        assert_eq!(hdr.seq_control, SeqControl { frag_num: 6, seq_num: 0xDEF });
        assert_eq!(hdr.ht_control, None);

        let body = AssociationResponseFields::from_reader(&mut bytes).expect("reading assoc resp");
        assert_eq!(body.capability_info, CapabilityInfo(0xBCDE));
        assert_eq!(body.status_code, 0x9876);
        assert_eq!(body.association_id, 0x6543);
    }

    #[test]
    fn simple_assoc() {
        let frame = MacFrameWriter::new(vec![])
            .association_response(
                &MgmtHeader {
                    frame_control: FrameControl(0),
                    duration: 0x8765,
                    addr1: [0x01, 0x02, 0x03, 0x04, 0x05, 0x06],
                    addr2: [0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C],
                    addr3: [0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12],
                    seq_control: SeqControl { frag_num: 6, seq_num: 0xDEF },
                    ht_control: None,
                },
                &AssociationResponseFields {
                    capability_info: CapabilityInfo(0xBCDE),
                    status_code: 0x9876,
                    association_id: 0x6543,
                },
            )
            .unwrap()
            .into_writer();
        #[rustfmt::skip]
        let expected_frame: &[u8] = &[
            // Framectl Duration    Address 1
            0x10, 0x00, 0x65, 0x87, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // Address 2                        Address 3
            0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
            // Seq ctl  Cap info    Status code Assoc id
            0xF6, 0xDE, 0xDE, 0xBC, 0x76, 0x98, 0x43, 0x65,
        ];
        assert_eq!(expected_frame, &frame[..]);
    }

    #[test]
    fn parse_data_frame() {
        let mut bytes = &[
            8u8, 1, // FrameControl
            0, 0, // Duration
            101, 116, 104, 110, 101, 116, // Addr1
            103, 98, 111, 110, 105, 107, // Addr2
            98, 115, 115, 98, 115, 115, // Addr3
            48, 0, // SeqControl
            // LLC Header
            170, 170, 3, // dsap, ssap and control
            0, 0, 0, // OUI
            0, 8, // protocol ID
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, // payload
        ] as &[u8];
        let data_header = DataHeader::from_reader(&mut bytes).expect("reading data header");
        assert_eq!(data_header.frame_control.typ(), FrameControlType::Data as u16);
        assert_eq!(data_header.frame_control.subtype(), DataSubtype::Data as u16);
        assert_eq!(data_header.addr1, [0x65, 0x74, 0x68, 0x6e, 0x65, 0x74]);
        assert_eq!(data_header.addr2, [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b]);
        assert_eq!(data_header.addr3, [0x62, 0x73, 0x73, 0x62, 0x73, 0x73]);
        assert_eq!(data_header.seq_control.seq_num, 3);
        assert!(data_header.addr4.is_none());
        assert!(data_header.qos_control.is_none());
        assert!(data_header.ht_control.is_none());

        let llc_header = LlcHeader::from_reader(&mut bytes).expect("reading llc header");
        assert_eq!(llc_header.oui, [0, 0, 0]);

        let mut payload = vec![];
        let bytes_read = io::Read::read_to_end(&mut bytes, &mut payload).expect("reading payload");
        assert_eq!(bytes_read, 15);
        assert_eq!(&payload[..], &[1u8, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
    }

    #[test]
    fn simple_data() {
        let frame = MacFrameWriter::new(vec![])
            .data(
                &DataHeader {
                    frame_control: FrameControl(0), // will be overwritten
                    duration: 0x8765,
                    addr1: [0x01, 0x02, 0x03, 0x04, 0x05, 0x06],
                    addr2: [0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C],
                    addr3: [0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12],
                    seq_control: SeqControl { frag_num: 6, seq_num: 0xDEF },
                    addr4: None,
                    qos_control: None,
                    ht_control: None,
                },
                &LlcHeader {
                    dsap: 123,
                    ssap: 234,
                    control: 111,
                    oui: [0xff, 0xfe, 0xfd],
                    protocol_id: 0x3456,
                },
                &[11, 12, 13, 14, 15, 16, 17],
            )
            .unwrap()
            .into_writer();
        #[rustfmt::skip]
        let expected_frame: &[u8] = &[
            0x08, 0x02,  // FrameControl (overwritten)
            0x65, 0x87,  // Duration
            1, 2, 3, 4, 5, 6,  // Addr1
            7, 8, 9, 10, 11, 12,  // Addr2
            13, 14, 15, 16, 17, 18,  // Addr3
            0xf6, 0xde,  // SeqControl
            // LLC Header
            123, 234, 111,  // dsap, ssap and control
            0xff, 0xfe, 0xfd,  // OUI
            0x56, 0x34,  // protocol ID
            // payload
            11, 12, 13, 14, 15, 16, 17,
        ];
        assert_eq!(expected_frame, &frame[..]);
    }
}
