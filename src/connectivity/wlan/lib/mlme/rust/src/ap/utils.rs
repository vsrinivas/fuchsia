// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{error::Error, RatesWriter},
    wlan_common::{
        appendable::Appendable,
        data_writer,
        ie::{self, *},
        mac::{self, Aid, Bssid, MacAddr, StatusCode},
        mgmt_writer,
        sequence::SequenceManager,
        TimeUnit,
    },
};

pub fn write_auth_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    auth_alg_num: mac::AuthAlgorithmNumber,
    auth_txn_seq_num: u16,
    status_code: StatusCode,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::AUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&client_addr) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, client_addr, bssid, seq_ctrl),
        None,
    )?;
    buf.append_value(&mac::AuthHdr { auth_alg_num, auth_txn_seq_num, status_code })?;
    Ok(())
}

pub fn write_assoc_resp_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    capabilities: mac::CapabilityInfo,
    status_code: StatusCode,
    aid: Aid,
    ies: &[u8],
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_RESP);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&client_addr) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, client_addr, bssid, seq_ctrl),
        None,
    )?;
    buf.append_value(&mac::AssocRespHdr { capabilities, status_code, aid })?;
    buf.append_value(ies)?;
    Ok(())
}

pub fn write_deauth_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    reason_code: mac::ReasonCode,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::DEAUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&client_addr) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, client_addr, bssid, seq_ctrl),
        None,
    )?;
    buf.append_value(&mac::DeauthHdr { reason_code })?;
    Ok(())
}

pub fn write_disassoc_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    reason_code: mac::ReasonCode,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::DISASSOC);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&client_addr) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, client_addr, bssid, seq_ctrl),
        None,
    )?;
    buf.append_value(&mac::DisassocHdr { reason_code })?;
    Ok(())
}

pub struct BeaconTemplate {
    pub buf: Vec<u8>,
    pub tim_ele_offset: usize,
}

pub fn make_beacon_template(
    bssid: Bssid,
    beacon_interval: TimeUnit,
    capabilities: mac::CapabilityInfo,
    ssid: &[u8],
    rates: &[u8],
    channel: u8,
    rsne: &[u8],
) -> Result<BeaconTemplate, Error> {
    let mut buf = vec![];

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::BEACON);
    mgmt_writer::write_mgmt_hdr(
        &mut buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, mac::BCAST_ADDR, bssid, mac::SequenceControl(0)),
        None,
    )?;
    buf.append_value(&mac::BeaconHdr { timestamp: 0, beacon_interval, capabilities })?;
    let rates_writer = RatesWriter::try_new(rates)?;

    // Order of beacon frame body IEs is according to IEEE Std 802.11-2016, Table 9-27, numbered
    // below.

    // 4. Service Set Identifier (SSID)
    write_ssid(&mut buf, ssid)?;

    // 5. Supported Rates and BSS Membership Selectors
    rates_writer.write_supported_rates(&mut buf);

    // 6. DSSS Parameter Set
    write_dsss_param_set(&mut buf, &DsssParamSet { current_chan: channel })?;

    // 9. Traffic indication map (TIM)
    // Write a placeholder TIM element, which the firmware will fill in. We only support hardware
    // with hardware offload beaconing for now (e.g. ath10k).
    let tim_ele_offset = buf.len();
    buf.append_value(&ie::Header { id: Id::TIM, body_len: 0 })?;

    // 17. Extended Supported Rates and BSS Membership Selectors
    rates_writer.write_ext_supported_rates(&mut buf);

    // 18. RSN
    // |rsne| already contains the IE prefix.
    buf.append_bytes(rsne)?;

    Ok(BeaconTemplate { buf, tim_ele_offset })
}

pub fn write_data_frame<B: Appendable>(
    buf: &mut B,
    seq_mgr: &mut SequenceManager,
    dst: MacAddr,
    bssid: Bssid,
    src: MacAddr,
    protected: bool,
    qos_ctrl: bool,
    ether_type: u16,
    payload: &[u8],
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::DATA)
        .with_data_subtype(mac::DataSubtype(0).with_qos(qos_ctrl))
        .with_protected(protected)
        .with_from_ds(true);

    // QoS is not fully supported. Write default, all zeroed QoS Control field.
    let qos_ctrl = if qos_ctrl { Some(mac::QosControl(0)) } else { None };
    let seq_ctrl = match qos_ctrl.as_ref() {
        None => mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&dst) as u16),
        Some(qos_ctrl) => {
            mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns2(&dst, qos_ctrl.tid()) as u16)
        }
    };
    data_writer::write_data_hdr(
        buf,
        mac::FixedDataHdrFields {
            frame_ctrl,
            duration: 0,
            addr1: dst,
            addr2: bssid.0,
            addr3: src,
            seq_ctrl,
        },
        mac::OptionalDataHdrFields { qos_ctrl, addr4: None, ht_ctrl: None },
    )?;

    data_writer::write_snap_llc_hdr(buf, ether_type)?;
    buf.append_bytes(payload)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        wlan_common::mac::{AuthAlgorithmNumber, Bssid},
    };

    #[test]
    fn auth_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_auth_frame(
            &mut buf,
            [1; 6],
            Bssid([2; 6]),
            &mut seq_mgr,
            AuthAlgorithmNumber::FAST_BSS_TRANSITION,
            3,
            StatusCode::TRANSACTION_SEQUENCE_ERROR,
        )
        .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b10110000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Auth body
                2, 0, // Auth Algorithm Number
                3, 0, // Auth Txn Seq Number
                14, 0, // Status code
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn assoc_resp_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_assoc_resp_frame(
            &mut buf,
            [1; 6],
            Bssid([2; 6]),
            &mut seq_mgr,
            mac::CapabilityInfo(0),
            StatusCode::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED,
            1,
            &[0, 4, 1, 2, 3, 4],
        )
        .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b00010000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Association response header:
                0, 0, // Capabilities
                94, 0, // status code
                1, 0, // AID
                0, 4, 1, 2, 3, 4, // SSID
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn disassoc_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_disassoc_frame(
            &mut buf,
            [1; 6],
            Bssid([2; 6]),
            &mut seq_mgr,
            mac::ReasonCode::LEAVING_NETWORK_DISASSOC,
        )
        .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b10100000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Disassoc header:
                8, 0, // reason code
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn beacon_template() {
        let template = make_beacon_template(
            Bssid([2; 6]),
            TimeUnit(10),
            mac::CapabilityInfo(33),
            &[1, 2, 3, 4, 5],
            &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            2,
            &[48, 2, 77, 88][..],
        )
        .expect("failed making beacon template");

        assert_eq!(
            &[
                // Mgmt header
                0b10000000, 0, // Frame Control
                0, 0, // Duration
                255, 255, 255, 255, 255, 255, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0, 0, // Sequence Control
                // Beacon header:
                0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
                10, 0, // Beacon interval
                33, 0, // Capabilities
                // IEs:
                0, 5, 1, 2, 3, 4, 5, // SSID
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Supported rates
                3, 1, 2, // DSSS parameter set
                5, 0, // TIM
                50, 2, 9, 10, // Extended rates
                48, 2, 77, 88, // RSNE
            ][..],
            &template.buf[..]
        );
        assert_eq!(template.tim_ele_offset, 56);
    }

    #[test]
    fn beacon_template_no_extended_rates() {
        let template = make_beacon_template(
            Bssid([2; 6]),
            TimeUnit(10),
            mac::CapabilityInfo(33),
            &[1, 2, 3, 4, 5],
            &[1, 2, 3, 4, 5, 6][..],
            2,
            &[48, 2, 77, 88][..],
        )
        .expect("failed making beacon template");
        assert_eq!(
            &[
                // Mgmt header
                0b10000000, 0, // Frame Control
                0, 0, // Duration
                255, 255, 255, 255, 255, 255, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0, 0, // Sequence Control
                // Beacon header:
                0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
                10, 0, // Beacon interval
                33, 0, // Capabilities
                // IEs:
                0, 5, 1, 2, 3, 4, 5, // SSID
                1, 6, 1, 2, 3, 4, 5, 6, // Supported rates
                3, 1, 2, // DSSS parameter set
                5, 0, // TIM
                48, 2, 77, 88, // RSNE
            ][..],
            &template.buf[..]
        );
        assert_eq!(template.tim_ele_offset, 54);
    }

    #[test]
    fn data_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_data_frame(
            &mut buf,
            &mut seq_mgr,
            [3; 6],
            Bssid([2; 6]),
            [1; 6],
            false,
            false,
            0x1234,
            &[1, 2, 3, 4, 5],
        )
        .expect("expected OK");
        assert_eq!(
            &[
                // Mgmt header
                0b00001000, 0b00000010, // Frame Control
                0, 0, // Duration
                3, 3, 3, 3, 3, 3, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0x10, 0, // Sequence Control
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
                0, 0, 0, // OUI
                0x12, 0x34, // Protocol ID
                // Data
                1, 2, 3, 4, 5,
            ][..],
            &buf[..]
        );
    }
}
