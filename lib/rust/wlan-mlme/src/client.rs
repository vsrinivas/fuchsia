// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    wlan_common::{mac, buffer_writer::{BufferWriter, ByteSliceMut}},
};

type MacAddr = [u8; 6];

/// Fills a given buffer with a null-data frame which will be send from a client to an AP.
/// Fails if the given buffer is too short.
/// Returns the amount of bytes written to the buffer.
pub fn write_keep_alive_resp_frame<B: ByteSliceMut>(buf: B, bssid: MacAddr,
                                                    client_addr: MacAddr, seq_ctrl: u16)
    -> Result<usize, Error>
{
    let (mut data_hdr, w) = BufferWriter::new(buf).reserve_zeroed::<mac::DataHdr>()?;
    let mut fc = mac::FrameControl(0);
    fc.set_frame_type(mac::FRAME_TYPE_DATA);
    fc.set_frame_subtype(mac::DATA_SUBTYPE_NULL_DATA);
    fc.set_to_ds(true);
    data_hdr.set_frame_ctrl(fc.value());
    data_hdr.addr1 = bssid;
    data_hdr.addr2 = client_addr;
    data_hdr.addr3 = bssid;
    data_hdr.set_seq_ctrl(seq_ctrl);
    Ok(w.written_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keep_alive_resp_frame() {
        let mut buf = [3u8; 25];
        let written_bytes = write_keep_alive_resp_frame(&mut buf[..], [1; 6], [2; 6], 42)
            .expect("failed writing frame");
        assert_eq!(24, written_bytes);
        assert_eq!([0b01001000, 0b1, // Frame Control
                    0, 0,  // Duration
                    1, 1, 1, 1, 1, 1,  // addr1
                    2, 2, 2, 2, 2, 2,  // addr2
                    1, 1, 1, 1, 1, 1,  // addr3
                    42, 0, // Sequence Control
                    3 // Trailing bytes
                   ], buf);
    }
}