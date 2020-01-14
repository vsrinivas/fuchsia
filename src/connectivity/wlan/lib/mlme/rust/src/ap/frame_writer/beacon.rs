// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    wlan_common::{
        appendable::Appendable,
        ie::{self, *},
        mac::{self, Bssid},
        mgmt_writer, TimeUnit,
    },
};

/// BeaconParams contains parameters that may be used to offload beaconing to the hardware.
pub struct BeaconOffloadParams {
    /// Offset from the start of the input buffer to the TIM element.
    pub tim_ele_offset: usize,
}

pub fn write_beacon_frame<B: Appendable>(
    buf: &mut B,
    bssid: Bssid,
    timestamp: u64,
    beacon_interval: TimeUnit,
    capabilities: mac::CapabilityInfo,
    ssid: &[u8],
    rates: &[u8],
    channel: u8,
    rsne: &[u8],
) -> Result<BeaconOffloadParams, Error> {
    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::BEACON);
    mgmt_writer::write_mgmt_hdr(
        buf,
        // The sequence control is 0 because the firmware will set it.
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, mac::BCAST_ADDR, bssid, mac::SequenceControl(0)),
        None,
    )?;
    buf.append_value(&mac::BeaconHdr { timestamp, beacon_interval, capabilities })?;
    let rates_writer = ie::RatesWriter::try_new(rates)?;

    // Order of beacon frame body IEs is according to IEEE Std 802.11-2016, Table 9-27, numbered
    // below.

    // 4. Service Set Identifier (SSID)
    write_ssid(buf, ssid)?;

    // 5. Supported Rates and BSS Membership Selectors
    rates_writer.write_supported_rates(buf);

    // 6. DSSS Parameter Set
    write_dsss_param_set(buf, &DsssParamSet { current_chan: channel })?;

    // 9. Traffic indication map (TIM)
    // Write a placeholder TIM element, which the firmware will fill in. We only support hardware
    // with hardware offload beaconing for now (e.g. ath10k).
    //
    // While this isn't a real TIM element, we still put 4 bytes in it as that is the minimum legal
    // TIM element value (IEEE Std 802.11-2016, 9.4.2.6: In the event that all bits other than bit 0
    // in the traffic indication virtual bitmap are 0, the Partial Virtual Bitmap field is encoded
    // as a single octet equal to 0, the Bitmap Offset subfield is 0, and the Length field is 4.)
    let tim_ele_offset = buf.bytes_written();
    buf.append_value(&ie::Header { id: Id::TIM, body_len: 4 })?;
    buf.append_bytes(
        &[
            0, // DTIM Count
            0, // DTIM Period
            0, // Bitmap Control
            0, // Partial Virtual Bitmap
        ][..],
    )?;

    // 17. Extended Supported Rates and BSS Membership Selectors
    rates_writer.write_ext_supported_rates(buf);

    // 18. RSN
    // |rsne| already contains the IE prefix.
    buf.append_bytes(rsne)?;

    Ok(BeaconOffloadParams { tim_ele_offset })
}

#[cfg(test)]
mod tests {
    use {super::*, wlan_common::mac::Bssid};

    #[test]
    fn beacon_frame() {
        let mut buf = vec![];
        let params = write_beacon_frame(
            &mut buf,
            Bssid([2; 6]),
            0,
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
                5, 4, 0, 0, 0, 0, // TIM
                50, 2, 9, 10, // Extended rates
                48, 2, 77, 88, // RSNE
            ][..],
            &buf[..]
        );
        assert_eq!(params.tim_ele_offset, 56);
    }

    #[test]
    fn beacon_frame_no_extended_rates() {
        let mut buf = vec![];
        let params = write_beacon_frame(
            &mut buf,
            Bssid([2; 6]),
            0,
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
                5, 4, 0, 0, 0, 0, // TIM
                48, 2, 77, 88, // RSNE
            ][..],
            &buf[..]
        );
        assert_eq!(params.tim_ele_offset, 54);
    }
}
