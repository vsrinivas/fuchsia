// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self};
use byteorder::BigEndian;
use byteorder::ByteOrder;
use std::{
    convert::TryInto,
    io::{Read, Write},
    os::raw::c_void as void,
    slice,
};
use tracing::*;

mod fastboot_c;
use self::fastboot_c::*;

pub const MESSAGE_LENGTH_PREFIX_BYTES: usize = 8;

extern "C" fn write_packet_callback<T: Read + Write>(
    data: *const ::std::os::raw::c_void,
    size: size_t,
    stream: *mut void,
) -> ::std::os::raw::c_int {
    // Get the stream writer from the context pointer.
    let stream: &mut T = unsafe { &mut *(stream as *mut T) };
    // Get the byte array payload to send.
    let slice = unsafe { slice::from_raw_parts(data as *const u8, size.try_into().unwrap()) };
    // Prepend with the length prefix.
    let mut packet = vec![0u8; MESSAGE_LENGTH_PREFIX_BYTES];
    BigEndian::write_u64(&mut packet, slice.len().try_into().unwrap());
    packet.extend(slice);
    // Send the packet over the stream.
    match stream.write_all(packet.as_slice()) {
        Ok(()) => {}
        Err(err) => {
            warn!(%err, "write_cb error");
            return 1;
        }
    }

    return 0;
}

extern "C" fn read_packet_callback<T: Read + Write>(
    data: *mut ::std::os::raw::c_void,
    packet_size: size_t,
    stream: *mut void,
) -> ::std::os::raw::c_int {
    // Get the stream writer from the context pointer.
    let stream: &mut T = unsafe { &mut *(stream as *mut T) };
    // Get the byte array payload to read into.
    let slice =
        unsafe { slice::from_raw_parts_mut(data as *mut u8, packet_size.try_into().unwrap()) };
    // Read requested number of bytes into the slice
    match stream.read_exact(slice) {
        Ok(()) => {}
        Err(err) => {
            warn!(%err, "read_packet error");
            return 1;
        }
    }

    return 0;
}

/// A safe rust wrapper for the fastboot_process() API
pub async fn fastboot_process_safe<T: Read + Write>(
    stream: &mut T,
    packet_size: usize,
) -> Result<(), anyhow::Error> {
    // Process the new packet
    let ret = unsafe {
        fastboot_process(
            packet_size.try_into().unwrap(),
            Some(read_packet_callback::<T>),
            Some(write_packet_callback::<T>),
            stream as *mut T as *mut void,
        )
    };
    match ret {
        0 => Ok(()),
        _ => {
            return Err(anyhow::format_err!("Failed to process packet. {}", ret));
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::HANDSHAKE_MESSAGE;
    use std::cmp::min;
    use std::io::{Error, ErrorKind};

    pub struct TestTransport {
        pub data_stream: Vec<u8>,
        pub sent_packets: Vec<Vec<u8>>,
        pub fail_write: bool,
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
            if self.fail_write {
                return Err(Error::new(ErrorKind::Other, "flag set"));
            }
            self.sent_packets.push(buf.to_vec());
            return Ok(buf.len());
        }

        fn flush(&mut self) -> std::io::Result<()> {
            // Irrelevant
            Ok(())
        }
    }

    #[fuchsia::test]
    async fn read_packet_callback_test() {
        let mut test_transport = TestTransport {
            data_stream: Vec::<u8>::new(),
            sent_packets: Vec::<Vec<u8>>::new(),
            fail_write: false,
        };
        test_transport.add_data_to_stream(HANDSHAKE_MESSAGE);
        const SIZE: usize = 4;
        let mut buf = [0u8; SIZE];
        assert_eq!(
            read_packet_callback::<TestTransport>(
                buf.as_mut_ptr() as *mut void,
                SIZE.try_into().unwrap(),
                &mut test_transport as *mut TestTransport as *mut void,
            ),
            0
        );
        assert_eq!(buf.to_vec(), HANDSHAKE_MESSAGE);
        assert_eq!(test_transport.data_stream.len(), 0);
    }

    #[fuchsia::test]
    async fn read_packet_callback_fail_test() {
        let mut test_transport = TestTransport {
            data_stream: Vec::<u8>::new(),
            sent_packets: Vec::<Vec<u8>>::new(),
            fail_write: false,
        };
        const SIZE: usize = 4;
        let mut buf = [0u8; SIZE];
        assert_eq!(
            read_packet_callback::<TestTransport>(
                buf.as_mut_ptr() as *mut void,
                SIZE.try_into().unwrap(),
                &mut test_transport as *mut TestTransport as *mut void,
            ),
            1
        );
    }

    #[fuchsia::test]
    async fn write_packet_callback_test() {
        let mut test_transport = TestTransport {
            data_stream: Vec::<u8>::new(),
            sent_packets: Vec::<Vec<u8>>::new(),
            fail_write: false,
        };
        assert_eq!(
            write_packet_callback::<TestTransport>(
                HANDSHAKE_MESSAGE.as_ptr() as *mut void,
                HANDSHAKE_MESSAGE.len().try_into().unwrap(),
                &mut test_transport as *mut TestTransport as *mut void,
            ),
            0
        );
        let mut expected = vec![0, 0, 0, 0, 0, 0, 0, 4];
        expected.extend(b"FB01");
        assert_eq!(test_transport.sent_packets, vec![expected]);
    }

    #[fuchsia::test]
    async fn write_packet_callback_failed_test() {
        let mut test_transport = TestTransport {
            data_stream: Vec::<u8>::new(),
            sent_packets: Vec::<Vec<u8>>::new(),
            fail_write: true,
        };
        assert_eq!(
            write_packet_callback::<TestTransport>(
                HANDSHAKE_MESSAGE.as_ptr() as *mut void,
                HANDSHAKE_MESSAGE.len().try_into().unwrap(),
                &mut test_transport as *mut TestTransport as *mut void,
            ),
            1
        );
    }
}
