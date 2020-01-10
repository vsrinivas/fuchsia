// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    big_endian::BigEndianU16,
    mac::{self, Bssid, FixedDataHdrFields, FrameControl, MacAddr, SequenceControl},
};

pub fn data_hdr_client_to_ap(
    mut frame_ctrl: FrameControl,
    bssid: Bssid,
    client_addr: MacAddr,
    seq_ctrl: SequenceControl,
) -> FixedDataHdrFields {
    frame_ctrl.set_to_ds(true);
    frame_ctrl.set_from_ds(false);
    FixedDataHdrFields {
        frame_ctrl,
        duration: 0,
        addr1: bssid.0,
        addr2: client_addr,
        addr3: bssid.0,
        seq_ctrl,
    }
}

pub fn make_snap_llc_hdr(protocol_id: u16) -> mac::LlcHdr {
    mac::LlcHdr {
        dsap: mac::LLC_SNAP_EXTENSION,
        ssap: mac::LLC_SNAP_EXTENSION,
        control: mac::LLC_SNAP_UNNUMBERED_INFO,
        oui: mac::LLC_SNAP_OUI,
        protocol_id: BigEndianU16::from_native(protocol_id),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn client_to_ap() {
        let got = data_hdr_client_to_ap(
            FrameControl(0b00110000_00110000),
            Bssid([1; 6]),
            [2; 6],
            SequenceControl(4321),
        );
        let expected = FixedDataHdrFields {
            frame_ctrl: FrameControl(0b00110001_00110000),
            duration: 0,
            addr1: [1; 6],
            addr2: [2; 6],
            addr3: [1; 6],
            seq_ctrl: SequenceControl(4321),
        };
        assert_eq!(got, expected);
    }
}
