// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use byteorder::BigEndian;
use fuchsia_syslog::{self, fx_log_info};
use std::{
    convert::TryInto,
    io::{Read, Write},
    net::{Ipv4Addr, Ipv6Addr, SocketAddr, TcpListener},
};

const HANDSHAKE_LENGTH: usize = 4;
const MESSAGE_LENGTH_PREFIX_BYTES: usize = 8;
const FASTBOOT_PORT: u16 = 5554;

// For simplicity, we only support version 01.
static HANDSHAKE_MESSAGE: &'static [u8; HANDSHAKE_LENGTH] = b"FB01";

// A process_packet() implementation only for test. It simply echoes back the
// packet.
#[cfg(test)]
async fn process_packet<T: Read + Write>(
    stream: &mut T,
    packet_size: usize,
) -> Result<(), anyhow::Error> {
    let mut packet = vec![0u8; packet_size];
    stream.read_exact(&mut packet)?;
    stream.write_all(&packet)?;
    Ok(())
}

#[cfg(not(test))]
async fn process_packet<T: Read + Write>(
    _stream: &mut T,
    _packet_size: usize,
) -> Result<(), anyhow::Error> {
    // TODO(b/217597389): Calls into the fastboot library to process the incoming packet.
    return Err(anyhow::format_err!("Not implemented"));
}

/// Start a fastboot session.
/// The function handles the handshake and listens for fastboot command/data from
/// the host within the same TCP session.
async fn fastboot_session<T: Read + Write>(stream: &mut T) -> Result<(), anyhow::Error> {
    // Perform handshake.
    let mut buf = [0u8; HANDSHAKE_LENGTH];
    stream.read_exact(&mut buf).context("")?;
    if buf.to_vec() != HANDSHAKE_MESSAGE {
        return Err(anyhow::format_err!("Invalid handshake message received. {:?}", &buf[..]));
    }
    // Reply with a handshake
    stream.write_all(HANDSHAKE_MESSAGE)?;
    // Keep listening for packet.
    loop {
        // Each fastboot tcp packet is preceded by a 8-byte length prefix.
        let mut length_prefix = [0u8; MESSAGE_LENGTH_PREFIX_BYTES];
        stream.read_exact(&mut length_prefix)?;
        // 8-byte endian to integer
        let packet_size: usize = u64::from_be_bytes(length_prefix).try_into().unwrap();
        process_packet(stream, packet_size).await?;
    }
}

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let addrs: [SocketAddr; 2] = [
        (Ipv6Addr::UNSPECIFIED, FASTBOOT_PORT).into(),
        (Ipv4Addr::UNSPECIFIED, FASTBOOT_PORT).into(),
    ];
    let listener = TcpListener::bind(&addrs[..]).context("Can't bind to address")?;
    loop {
        let (mut stream, _) = listener.accept().context("Accept failed")?;
        // Set read time out to 1 seconds. Each fastboot host command initiates a new
        // TCP connection and sends one or more command/data packets. It closes itself
        // after finished. The timeout is used as a simple solution to close connection
        // when host finishes and becomes idle.
        stream.set_read_timeout(Some(std::time::Duration::new(1, 0)))?;
        match fastboot_session(&mut stream).await {
            Ok(()) => {}
            Err(e) => {
                fx_log_info!("session error: {}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use byteorder::ByteOrder;
    use std::cmp::min;
    use std::io::{Error, ErrorKind};

    struct TestTransport {
        data_stream: Vec<u8>,
        sent_packets: Vec<Vec<u8>>,
    }

    impl TestTransport {
        pub fn add_data_to_stream(&mut self, buf: &[u8]) {
            self.data_stream.extend(buf.to_vec());
        }

        pub fn add_packet_to_stream(&mut self, buf: &[u8]) {
            let mut packet = vec![0u8; MESSAGE_LENGTH_PREFIX_BYTES];
            BigEndian::write_u64(&mut packet, buf.len().try_into().unwrap());
            self.data_stream.extend(&packet);
            self.data_stream.extend(buf.to_vec());
        }
    }

    impl Read for TestTransport {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            if self.data_stream.len() == 0 {
                return Err(Error::new(ErrorKind::Other, "no more test data"));
            }
            let to_read = min(self.data_stream.len(), buf.len());
            buf.clone_from_slice(&self.data_stream[..to_read]);
            self.data_stream = self.data_stream[to_read..].to_vec();
            return Ok(to_read as usize);
        }
    }

    impl Write for TestTransport {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            self.sent_packets.push(buf.to_vec());
            return Ok(buf.len());
        }

        fn flush(&mut self) -> std::io::Result<()> {
            // Irrelevant
            Ok(())
        }
    }

    #[fuchsia::test]
    async fn fastboot_session_test() {
        let mut test_transport =
            TestTransport { data_stream: Vec::<u8>::new(), sent_packets: Vec::<Vec<u8>>::new() };
        test_transport.add_data_to_stream(b"FB01");
        test_transport.add_packet_to_stream(b"getvar:all");
        test_transport.add_packet_to_stream(b"flash:bootloader");

        let err = fastboot_session(&mut test_transport).await.unwrap_err();
        assert_eq!(err.to_string(), "no more test data");

        assert_eq!(
            test_transport.sent_packets,
            vec![
                b"FB01".to_vec(),             // handshake
                b"getvar:all".to_vec(),       // first packet
                b"flash:bootloader".to_vec(), // second packet
            ]
        );
    }
}
