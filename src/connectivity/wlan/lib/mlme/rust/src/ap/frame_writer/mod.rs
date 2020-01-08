// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod beacon;

use {
    crate::{error::Error, RatesWriter},
    anyhow::format_err,
    wlan_common::{
        appendable::Appendable,
        data_writer,
        ie::*,
        mac::{self, Aid, Bssid, MacAddr, StatusCode},
        mgmt_writer,
        sequence::SequenceManager,
        TimeUnit,
    },
    zerocopy::LayoutVerified,
};

pub use beacon::*;

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
    aid: Aid,
    rates: &[u8],
    max_idle_period: Option<u16>,
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

    buf.append_value(&mac::AssocRespHdr { capabilities, status_code: StatusCode::SUCCESS, aid })?;

    // Order of association response frame body IEs is according to IEEE Std 802.11-2016,
    // Table 9-30, numbered below.

    let rates_writer = RatesWriter::try_new(&rates[..])?;

    // 4: Supported Rates and BSS Membership Selectors
    rates_writer.write_supported_rates(buf);

    // 5: Extended Supported Rates and BSS Membership Selectors
    rates_writer.write_ext_supported_rates(buf);

    if let Some(max_idle_period) = max_idle_period {
        // 19: BSS Max Idle Period
        write_bss_max_idle_period(
            buf,
            &BssMaxIdlePeriod {
                max_idle_period,
                idle_options: IdleOptions(0)
                    // TODO(37891): Support configuring this.
                    .with_protected_keep_alive_required(false),
            },
        )?;
    }

    Ok(())
}

pub fn write_assoc_resp_frame_error<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    capabilities: mac::CapabilityInfo,
    status_code: StatusCode,
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
    buf.append_value(&mac::AssocRespHdr { capabilities, status_code, aid: 0 })?;
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

pub fn write_probe_resp_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    bssid: Bssid,
    seq_mgr: &mut SequenceManager,
    timestamp: u64,
    beacon_interval: TimeUnit,
    capabilities: mac::CapabilityInfo,
    ssid: &[u8],
    rates: &[u8],
    channel: u8,
    rsne: &[u8],
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::PROBE_RESP);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&client_addr) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        // The sequence control is 0 because the firmware will set it.
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, client_addr, bssid, seq_ctrl),
        None,
    )?;

    buf.append_value(&mac::ProbeRespHdr { timestamp, beacon_interval, capabilities })?;

    let rates_writer = RatesWriter::try_new(rates)?;

    // Order of beacon frame body IEs is according to IEEE Std 802.11-2016, Table 9-27, numbered
    // below.

    // 4. Service Set Identifier (SSID)
    write_ssid(buf, ssid)?;

    // 5. Supported Rates and BSS Membership Selectors
    rates_writer.write_supported_rates(buf);

    // 6. DSSS Parameter Set
    write_dsss_param_set(buf, &DsssParamSet { current_chan: channel })?;

    // 16. Extended Supported Rates and BSS Membership Selectors
    rates_writer.write_ext_supported_rates(buf);

    // 17. RSN
    // |rsne| already contains the IE prefix.
    buf.append_bytes(rsne)?;

    Ok(())
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

pub fn set_more_data(buf: &mut [u8]) -> Result<(), Error> {
    let (frame_ctrl, _) = LayoutVerified::<&mut [u8], mac::FrameControl>::new_from_prefix(buf)
        .ok_or(format_err!("could not parse frame control header"))?;
    let frame_ctrl = frame_ctrl.into_mut();
    frame_ctrl.set_more_data(true);
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
            1,
            &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            Some(99),
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
                0, 0, // status code
                1, 0, // AID
                // IEs
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Rates
                50, 2, 9, 10, // Extended rates
                90, 3, 99, 0, 0, // BSS max idle period
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn assoc_resp_frame_error() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_assoc_resp_frame_error(
            &mut buf,
            [1; 6],
            Bssid([2; 6]),
            &mut seq_mgr,
            mac::CapabilityInfo(0),
            StatusCode::REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED,
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
                0, 0, // AID
            ][..],
            &buf[..]
        );
    }

    #[test]
    fn probe_resp_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_probe_resp_frame(
            &mut buf,
            [1; 6],
            Bssid([2; 6]),
            &mut seq_mgr,
            0,
            TimeUnit(10),
            mac::CapabilityInfo(33),
            &[1, 2, 3, 4, 5],
            &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..],
            2,
            &[48, 2, 77, 88][..],
        )
        .expect("failed writing probe response");

        assert_eq!(
            &[
                // Mgmt header
                0b01010000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                2, 2, 2, 2, 2, 2, // addr3
                0x10, 0, // Sequence Control
                // Beacon header:
                0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
                10, 0, // Beacon interval
                33, 0, // Capabilities
                // IEs:
                0, 5, 1, 2, 3, 4, 5, // SSID
                1, 8, 1, 2, 3, 4, 5, 6, 7, 8, // Supported rates
                3, 1, 2, // DSSS parameter set
                50, 2, 9, 10, // Extended rates
                48, 2, 77, 88, // RSNE
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

    #[test]
    fn more_data() {
        let mut buf = vec![
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
        ];
        set_more_data(&mut buf[..]).expect("expected set more data OK");
        assert_eq!(
            &[
                // Mgmt header
                0b00001000, 0b00100010, // Frame Control
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
