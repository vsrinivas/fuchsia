// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{auth, error::Error, RatesWriter},
    failure::format_err,
    wlan_common::{
        appendable::Appendable,
        data_writer,
        ie::{rsn::rsne, *},
        mac::{self, Aid, Bssid, MacAddr, PowerState},
        mgmt_writer,
        sequence::SequenceManager,
    },
};

pub fn write_open_auth_frame<B: Appendable>(
    buf: &mut B,
    bssid: Bssid,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::AUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&bssid.0) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        None,
    )?;

    buf.append_value(&auth::make_open_client_req())?;
    Ok(())
}

#[allow(unused)]
pub fn write_assoc_req_frame<B: Appendable>(
    buf: &mut B,
    bssid: Bssid,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
    cap_info: mac::CapabilityInfo,
    ssid: &[u8],
    rates: &[u8],
    rsne: Option<rsne::Rsne>,
    ht_cap: Option<HtCapabilities>,
    vht_cap: Option<VhtCapabilities>,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_REQ);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&bssid.0) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        None,
    )?;

    buf.append_value(&mac::AssocReqHdr { capabilities: cap_info, listen_interval: 0 })?;

    write_ssid(buf, ssid)?;
    let rates_writer = RatesWriter::try_new(rates)?;
    rates_writer.write_supported_rates(buf);
    rates_writer.write_ext_supported_rates(buf);

    if let Some(rsne) = rsne {
        write_rsne(buf, &rsne)?;
    }

    if let Some(ht_cap) = ht_cap {
        write_ht_capabilities(buf, &ht_cap)?;
        if let Some(vht_cap) = vht_cap {
            write_vht_capabilities(buf, &vht_cap)?;
        }
    } else {
        if vht_cap.is_some() {
            return Err(Error::Internal(format_err!("vht_ap without ht_ap is invalid")));
        }
    }

    Ok(())
}

pub fn write_deauth_frame<B: Appendable>(
    buf: &mut B,
    bssid: Bssid,
    client_addr: MacAddr,
    reason_code: mac::ReasonCode,
    seq_mgr: &mut SequenceManager,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::DEAUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&bssid.0) as u16);
    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        None,
    )?;

    buf.append_value(&mac::DeauthHdr { reason_code })?;
    Ok(())
}

/// Fills a given buffer with a null-data frame.
pub fn write_keep_alive_resp_frame<B: Appendable>(
    buf: &mut B,
    bssid: Bssid,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::DATA)
        .with_data_subtype(mac::DataSubtype(0).with_null(true));
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&bssid.0) as u16);

    data_writer::write_data_hdr(
        buf,
        data_writer::data_hdr_client_to_ap(frame_ctrl, bssid, client_addr, seq_ctrl),
        mac::OptionalDataHdrFields::none(),
    )?;
    Ok(())
}

/// If |qos_ctrl| is true a default QoS Control field, all zero, is written to the frame as
/// Fuchsia's WLAN client does not support full QoS yet. The QoS Control is written to support
/// higher PHY rates such as HT and VHT which mandate QoS usage.
pub fn write_data_frame<B: Appendable>(
    buf: &mut B,
    seq_mgr: &mut SequenceManager,
    bssid: Bssid,
    src: MacAddr,
    dst: MacAddr,
    protected: bool,
    qos_ctrl: bool,
    ether_type: u16,
    payload: &[u8],
) -> Result<(), Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::DATA)
        .with_data_subtype(mac::DataSubtype(0).with_qos(qos_ctrl))
        .with_protected(protected)
        .with_to_ds(true);

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
            addr1: bssid.0,
            addr2: src,
            addr3: dst,
            seq_ctrl,
        },
        mac::OptionalDataHdrFields { qos_ctrl, addr4: None, ht_ctrl: None },
    )?;

    data_writer::write_snap_llc_hdr(buf, ether_type)?;
    buf.append_bytes(payload)?;
    Ok(())
}

pub fn write_ps_poll_frame<B: Appendable>(
    buf: &mut B,
    aid: Aid,
    bssid: Bssid,
    ta: MacAddr,
) -> Result<(), Error> {
    const PS_POLL_ID_MASK: u16 = 0b11000000_00000000;

    buf.append_value(&mac::PsPoll {
        frame_ctrl: mac::FrameControl(0)
            .with_frame_type(mac::FrameType::CTRL)
            .with_ctrl_subtype(mac::CtrlSubtype::PS_POLL),
        // IEEE 802.11-2016 9.3.1.5 states the ID in the PS-Poll frame is the association ID with
        // the 2 MSBs set to 1.
        id: aid | PS_POLL_ID_MASK,
        bssid: bssid,
        ta: ta,
    })?;

    Ok(())
}

pub fn write_power_state_frame<B>(
    buffer: &mut B,
    bssid: Bssid,
    source: MacAddr,
    sequencer: &mut SequenceManager,
    state: PowerState,
) -> Result<(), Error>
where
    B: Appendable,
{
    let control = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::DATA)
        .with_data_subtype(mac::DataSubtype(0).with_null(true))
        .with_to_ds(true)
        .with_power_mgmt(state);
    data_writer::write_data_hdr(
        buffer,
        mac::FixedDataHdrFields {
            frame_ctrl: control,
            duration: 0,
            addr1: bssid.0,
            addr2: source,
            addr3: bssid.0,
            seq_ctrl: mac::SequenceControl(0).with_seq_num(sequencer.next_sns1(&bssid.0) as u16),
        },
        mac::OptionalDataHdrFields::none(),
    )
    .map_err(|error| error.into())
}

pub fn write_probe_req_frame<B: Appendable>(
    buf: &mut B,
    client_addr: MacAddr,
    seq_mgr: &mut SequenceManager,
    ssid: &[u8],
    rates: &[u8],
    ht_cap: Option<HtCapabilities>,
    vht_cap: Option<VhtCapabilities>,
) -> Result<(), Error> {
    if ht_cap.is_none() && vht_cap.is_some() {
        return Err(Error::Internal(format_err!("vht_cap without ht_cap is invalid")));
    }

    let bcast_addr = Bssid(mac::BCAST_ADDR);
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(seq_mgr.next_sns1(&bcast_addr.0) as u16);

    mgmt_writer::write_mgmt_hdr(
        buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, bcast_addr, client_addr, seq_ctrl),
        None,
    )?;

    write_ssid(buf, ssid)?;
    let rates_writer = RatesWriter::try_new(&rates[..])?;
    rates_writer.write_supported_rates(buf);
    rates_writer.write_ext_supported_rates(buf);

    if let Some(ht_cap) = ht_cap {
        write_ht_capabilities(buf, &ht_cap)?;
        if let Some(vht_cap) = vht_cap {
            write_vht_capabilities(buf, &vht_cap)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        wlan_common::{assert_variant, buffer_writer::BufferWriter},
    };

    #[test]
    fn open_auth_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_open_auth_frame(&mut buf, Bssid([1; 6]), [2; 6], &mut seq_mgr)
            .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b10110000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0x10, 0, // Sequence Control
                // Auth body
                0, 0, // Auth Algorithm Number
                1, 0, // Auth Txn Seq Number
                0, 0, // Status code
            ],
            &buf[..]
        );
    }

    #[test]
    fn deauth_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_deauth_frame(&mut buf, Bssid([1; 6]), [2; 6], mac::ReasonCode::TIMEOUT, &mut seq_mgr)
            .expect("failed writing frame");
        assert_eq!(
            &[
                // Mgmt header
                0b11000000, 0, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0x10, 0, // Sequence Control
                // Deauth body
                0x27, 0 // Reason code
            ],
            &buf[..]
        );
    }

    #[test]
    fn assoc_req_frame_ok() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        let rsne = rsne::from_bytes(&wlan_common::test_utils::fake_frames::fake_wpa2_rsne()[..])
            .expect("creating RSNE")
            .1;

        write_assoc_req_frame(
            &mut buf,
            Bssid([1; 6]),
            [2; 6],
            &mut seq_mgr,
            mac::CapabilityInfo(0x1234),
            &"ssid".as_bytes(),
            &[8, 7, 6, 5, 4, 3, 2, 1, 0],
            Some(rsne),
            Some(wlan_common::ie::fake_ht_capabilities()),
            Some(wlan_common::ie::fake_vht_capabilities()),
        )
        .expect("writing association request frame");

        assert_eq!(
            &buf[..],
            &[
                // Mgmt Header
                0, 0, // frame control
                0, 0, // duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0x10, 0, // sequence control
                // Association Request Header
                0x34, 0x12, // capability info
                0, 0, // listen interval
                // IEs
                0, 4, // SSID id and length
                115, 115, 105, 100, // SSID
                1, 8, // supp_rates id and length
                8, 7, 6, 5, 4, 3, 2, 1, // supp_rates
                50, 1, // ext_supp rates id and length
                0, // ext_supp_rates
                48, 18, // rsne id and length
                1, 0, 0, 15, 172, 4, 1, 0, // rsne
                0, 15, 172, 4, 1, 0, 0, 15, // rsne
                172, 2, // rsne (18 bytes total)
                45, 26, // ht_cap id and length
                254, 1, 0, 255, 0, 0, 0, 1, // ht_cap
                0, 0, 0, 0, 0, 0, 0, 1, // ht_cap
                0, 0, 0, 0, 0, 0, 0, 0, // ht_cap
                0, 0, // ht_cap (26 bytes total)
                191, 12, // vht_cap id and length
                177, 2, 0, 177, 3, 2, 99, 67, // vht_cap
                3, 2, 99, 3 // vht_cap (12 bytes total)
            ][..]
        );
    }

    #[test]
    fn assoc_req_frame_vht_without_ht_error() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();

        assert!(write_assoc_req_frame(
            &mut buf,
            Bssid([1; 6]),
            [2; 6],
            &mut seq_mgr,
            mac::CapabilityInfo(0),
            &"ssid".as_bytes(),
            &[0],
            None, // RSNE
            None, // HT Capabilities
            Some(wlan_common::ie::fake_vht_capabilities()),
        )
        .is_err());
    }

    #[test]
    fn keep_alive_resp_frame() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_keep_alive_resp_frame(&mut buf, Bssid([1; 6]), [2; 6], &mut seq_mgr)
            .expect("failed writing frame");
        assert_eq!(
            &[
                0b01001000, 0b1, // Frame Control
                0, 0, // Duration
                1, 1, 1, 1, 1, 1, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                1, 1, 1, 1, 1, 1, // addr3
                0x10, 0, // Sequence Control
            ],
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
            Bssid([1; 6]),
            [2; 6],
            [3; 6],
            false, // protected
            false, // qos_ctrl
            0xABCD,
            &[4, 5, 6],
        )
        .expect("failed writing data frame");
        let expected = [
            // Data header
            0b00001000, 0b00000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            // LLC header
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0xAB, 0xCD, // Protocol ID
            // Payload
            4, 5, 6,
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn data_frame_protected_qos() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_data_frame(
            &mut buf,
            &mut seq_mgr,
            Bssid([1; 6]),
            [2; 6],
            [3; 6],
            true, // protected
            true, // qos_ctrl
            0xABCD,
            &[4, 5, 6],
        )
        .expect("failed writing data frame");
        let expected = [
            // Data header
            0b10001000, 0b01000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            0, 0, // QoS Control
            // LLC header
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0xAB, 0xCD, // Protocol ID
            // Payload
            4, 5, 6,
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn data_frame_empty_payload() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_data_frame(
            &mut buf,
            &mut seq_mgr,
            Bssid([1; 6]),
            [2; 6],
            [3; 6],
            true,  // protected
            false, // qos_ctrl
            0xABCD,
            &[],
        )
        .expect("failed writing frame");
        let expected = [
            // Data header
            0b00001000, 0b01000001, // Frame Control
            0, 0, // Duration
            1, 1, 1, 1, 1, 1, // addr1
            2, 2, 2, 2, 2, 2, // addr2
            3, 3, 3, 3, 3, 3, // addr3
            0x10, 0, // Sequence Control
            // LLC header
            0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
            0, 0, 0, // OUI
            0xAB, 0xCD, // Protocol ID
        ];
        assert_eq!(&expected[..], &buf[..]);
    }

    #[test]
    fn data_frame_buffer_too_small() {
        let mut buf = [99u8; 34];
        let mut seq_mgr = SequenceManager::new();
        let result = write_data_frame(
            &mut BufferWriter::new(&mut buf[..]),
            &mut seq_mgr,
            Bssid([1; 6]),
            [2; 6],
            [3; 6],
            false, // protected
            false, // qos_ctrl
            0xABCD,
            &[4, 5, 6],
        );
        assert_variant!(result, Err(Error::BufferTooSmall));
    }

    #[test]
    fn ps_poll_frame() {
        let mut buf = vec![];
        write_ps_poll_frame(&mut buf, 0b00100000_00100001, Bssid([1; 6]), [2; 6])
            .expect("failed writing frame");
        let expected = [
            0b10100100, 0, // Frame control
            0b00100001, 0b11100000, // ID (2 MSBs are set to 1 from the AID)
            1, 1, 1, 1, 1, 1, // BSSID
            2, 2, 2, 2, 2, 2, // TA
        ];
        assert_eq!(&expected[..], &buf[..])
    }

    #[test]
    fn power_state_frame() {
        let mut buffer = vec![];
        let mut sequencer = SequenceManager::new();
        write_power_state_frame(
            &mut buffer,
            Bssid([1; 6]),
            [2; 6],
            &mut sequencer,
            PowerState::DOZE,
        )
        .expect("failed writing frame");
        assert_eq!(
            &[
                0b01001000, 0b00010001, // Frame control.
                0, 0, // Duration.
                1, 1, 1, 1, 1, 1, // BSSID.
                2, 2, 2, 2, 2, 2, // MAC address.
                1, 1, 1, 1, 1, 1, // BSSID.
                0x10, 0, // Sequence control.
            ],
            &buffer[..]
        );
    }
    #[test]
    fn probe_req_frame_ok() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();
        write_probe_req_frame(
            &mut buf,
            [2; 6],
            &mut seq_mgr,
            &"ssid".as_bytes(),
            &[8, 7, 6, 5, 4, 3, 2, 1, 0],
            Some(wlan_common::ie::fake_ht_capabilities()),
            Some(wlan_common::ie::fake_vht_capabilities()),
        )
        .expect("writing probe request frame");

        #[rustfmt::skip]
        assert_eq!(
            &buf[..],
            &[
                // Mgmt Header
                0b01000000, 0, // frame control
                0, 0, // duration
                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr1
                2, 2, 2, 2, 2, 2, // addr2
                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // addr3
                0x10, 0, // sequence control
                // IEs
                0, 4, // SSID id and length
                115, 115, 105, 100, // SSID
                1, 8, // supp_rates id and length
                8, 7, 6, 5, 4, 3, 2, 1, // supp_rates
                50, 1, // ext_supp rates id and length
                0, // ext_supp_rates
                45, 26, // ht_cap id and length
                254, 1, 0, 255, 0, 0, 0, 1, // ht_cap
                0, 0, 0, 0, 0, 0, 0, 1, // ht_cap
                0, 0, 0, 0, 0, 0, 0, 0, // ht_cap
                0, 0, // ht_cap (26 bytes total)
                191, 12, // vht_cap id and length
                177, 2, 0, 177, 3, 2, 99, 67, // vht_cap
                3, 2, 99, 3 // vht_cap (12 bytes total)
            ][..]
        );
    }

    #[test]
    fn probe_req_frame_error() {
        let mut buf = vec![];
        let mut seq_mgr = SequenceManager::new();

        assert!(write_probe_req_frame(
            &mut buf,
            [2; 6],
            &mut seq_mgr,
            &"ssid".as_bytes(),
            &[0],
            None, // HT Capabilities
            Some(wlan_common::ie::fake_vht_capabilities()),
        )
        .is_err());
    }
}
