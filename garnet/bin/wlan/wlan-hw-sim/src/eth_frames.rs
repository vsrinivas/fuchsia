// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt},
    std::io,
};

#[derive(Clone, Copy, Debug)]
pub enum EtherType {
    Ipv4 = 0x0800,
}

// IEEE Std 802.3-2015, 3.1.1
#[derive(Clone, Copy, Debug)]
pub struct EthHeader {
    pub dst: [u8; 6],
    pub src: [u8; 6],
    pub eth_type: u16,
}

impl EthHeader {
    pub fn from_reader<R: io::Read>(reader: &mut R) -> io::Result<Self> {
        let mut dst = [0u8; 6];
        reader.read(&mut dst)?;
        let mut src = [0u8; 6];
        reader.read(&mut src)?;
        let eth_type = reader.read_u16::<LittleEndian>()?;
        Ok(Self { dst, src, eth_type })
    }
}

pub fn write_eth_header<W: io::Write>(w: &mut W, header: &EthHeader) -> io::Result<()> {
    w.write_all(&header.dst)?;
    w.write_all(&header.src)?;
    w.write_u16::<LittleEndian>(header.eth_type)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn simple_eth_header() {
        let mut buf = vec![];
        write_eth_header(
            &mut buf,
            &EthHeader {
                dst: [0x01, 0x02, 0x03, 0x04, 0x05, 0x06],
                src: [0x11, 0x12, 0x13, 0x14, 0x15, 0x16],
                eth_type: EtherType::Ipv4 as u16,
            },
        )
        .expect("Error writing ethernet header");
        #[rustfmt::skip]
        let expected_frame: &[u8] = &[
            // dst
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // src
            0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
            // ether type
            0x00, 0x08,
        ];
        assert_eq!(expected_frame, &buf[..]);
    }

    #[test]
    fn parse_eth_header() {
        let mut bytes = &[
            0x01u8, 0x02, 0x03, 0x04, 0x05, 0x06, // dst
            0x11, 0x12, 0x13, 0x14, 0x15, 0x16, // src
            0x00, 0x08, // ether_type
        ] as &[u8];
        let eth_header = EthHeader::from_reader(&mut bytes).expect("reading eth_header");
        assert_eq!(eth_header.dst, [0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);
        assert_eq!(eth_header.src, [0x11, 0x12, 0x13, 0x14, 0x15, 0x16]);
        assert_eq!(eth_header.eth_type, EtherType::Ipv4 as u16);
    }
}
