// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    wlan_common::{
        appendable::Appendable,
        big_endian::BigEndianU16,
        mac::{self, MacAddr},
    },
};

pub fn write_eth_frame<B: Appendable>(
    buf: &mut B,
    dst_addr: MacAddr,
    src_addr: MacAddr,
    protocol_id: u16,
    body: &[u8],
) -> Result<(), Error> {
    buf.append_value(&mac::EthernetIIHdr {
        da: dst_addr,
        sa: src_addr,
        ether_type: BigEndianU16::from_native(protocol_id),
    })?;
    buf.append_bytes(body)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        wlan_common::{assert_variant, buffer_writer::BufferWriter},
    };

    #[test]
    fn eth_frame_ok() {
        let mut buf = vec![];
        write_eth_frame(&mut buf, [1; 6], [2; 6], 3333, &[4; 9])
            .expect("failed writing ethernet frame");
        assert_eq!(
            &[
                1, 1, 1, 1, 1, 1, // dst_addr
                2, 2, 2, 2, 2, 2, // src_addr
                0x0d, 0x05, // ether_type
                4, 4, 4, 4, 4, 4, 4, 4, // payload
                4, // more payload
            ],
            &buf[..]
        );
    }

    #[test]
    fn eth_frame_buffer_too_small() {
        let mut buf = [7u8; 22];
        let write_result =
            write_eth_frame(&mut BufferWriter::new(&mut buf[..]), [1; 6], [2; 6], 3333, &[4; 9]);
        assert_variant!(write_result, Err(Error::BufferTooSmall));
    }

    #[test]
    fn eth_frame_empty_payload() {
        let mut buf = vec![];
        write_eth_frame(&mut buf, [1; 6], [2; 6], 3333, &[])
            .expect("failed writing ethernet frame");
        assert_eq!(
            &[
                1, 1, 1, 1, 1, 1, // dst_addr
                2, 2, 2, 2, 2, 2, // src_addrfx
                0x0d, 0x05, // ether_type
            ],
            &buf[..]
        );
    }
}
