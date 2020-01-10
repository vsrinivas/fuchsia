// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::error::Error, anyhow::format_err, wlan_common::mac, zerocopy::LayoutVerified};

pub fn set_more_data(buf: &mut [u8]) -> Result<(), Error> {
    let (frame_ctrl, _) = LayoutVerified::<&mut [u8], mac::FrameControl>::new_from_prefix(buf)
        .ok_or(format_err!("could not parse frame control header"))?;
    let frame_ctrl = frame_ctrl.into_mut();
    frame_ctrl.set_more_data(true);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

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
