// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for netdump's PCAPNG output format.
//! These tests are not intended for verifying PCAPNG formats in general, only that used by netdump.

use {
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    helper::*,
    net_types::ip::IpVersion,
    packet::BufferView,
    std::net::Ipv4Addr,
    std::net::UdpSocket,
    std::path::Path,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

mod pcapng;
use crate::pcapng::*;

const PACKET_COUNT: usize = 5;
const PAYLOAD_LENGTH: usize = 100; // octets.
const ETHERNET_LENGTH: usize = 14; // header octets.
const IPV4_LENGTH: usize = 20; // header octets.
const UDP_LENGTH: usize = 8; // header octets.

fn main() -> Result<(), Error> {
    // `pcapng_format` as test name causes maximum endpoint name length to be exceeded.
    const TEST_NAME: &str = "pcapng";
    let mut env = TestEnvironment::new(fasync::Executor::new()?, TEST_NAME)?;

    // Checks that the block structs have the right sizes in memory.
    assert_eq!(std::mem::size_of::<SectionHeader>(), SECTION_HEADER_LENGTH);
    assert_eq!(std::mem::size_of::<InterfaceDescription>(), INTERFACE_DESCRIPTION_LENGTH);
    assert_eq!(std::mem::size_of::<SimplePacket>(), SIMPLE_PACKET_LENGTH);

    test_case!(good_format_test,);
    test_case!(bad_format_test,);
    test_case!(pcapng_no_packets_test, &mut env);
    test_case!(pcapng_packet_headers_test, &mut env);
    test_case!(pcapng_fake_packets_test, &mut env);

    Ok(())
}

fn read_default_dumpfile() -> Result<Vec<u8>, Error> {
    let path_str = format!("{}/{}", DUMPFILE_DIR, DEFAULT_DUMPFILE);
    let path = Path::new(&path_str);
    let file = std::fs::read(&path)?;
    Ok(file)
}

// The `is_valid_block` calls on each block should pass with good test vectors.
fn good_format_test() -> Result<(), Error> {
    let shb = SectionHeader {
        btype: 0x0A0D0D0A,
        tot_length: 68,
        magic: 0x1A2B3C4D,
        major: 1,
        minor: 0,
        section_length: -1,
        tot_length2: 68,
    };

    let idb = InterfaceDescription {
        btype: 1,
        tot_length: 24,
        linktype: 1,
        reserved: 0xFFFF,
        snaplen: 0xABCD,
        tot_length2: 24,
    };

    let spb = SimplePacket { btype: 3, tot_length: 56, packet_length: 40 };

    assert!(shb.is_valid_block().is_ok());
    assert!(idb.is_valid_block().is_ok());
    assert!(spb.is_valid_block().is_ok());
    Ok(())
}

// The `is_valid_block` calls on each block should fail given invalid data.
fn bad_format_test() -> Result<(), Error> {
    let shb: [u8; SECTION_HEADER_LENGTH] = [37; SECTION_HEADER_LENGTH];
    let idb: [u8; INTERFACE_DESCRIPTION_LENGTH] = [24; INTERFACE_DESCRIPTION_LENGTH];
    let spb: [u8; SIMPLE_PACKET_LENGTH] = [42; SIMPLE_PACKET_LENGTH];

    let mut shb_slice = &mut &shb[..];
    let mut idb_slice = &mut &idb[..];
    let mut spb_slice = &mut &spb[..];

    let section_header = shb_slice.take_obj_front::<SectionHeader>().unwrap();
    let interface_description = idb_slice.take_obj_front::<InterfaceDescription>().unwrap();
    let simple_packet = spb_slice.take_obj_front::<SimplePacket>().unwrap();

    assert!(section_header.into_ref().is_valid_block().is_err());
    assert!(interface_description.into_ref().is_valid_block().is_err());
    assert!(simple_packet.into_ref().is_valid_block().is_err());

    Ok(())
}

// Consume section header and interface description blocks.
// Returns unconsumed left-over `buf_mut`.
fn consume_shb_idb<'a>(mut buf_mut: &'a mut &'a [u8]) -> Result<&'a mut &'a [u8], Error> {
    let section_header = buf_mut
        .take_obj_front::<SectionHeader>()
        .expect("Data cannot be read as a section header block!");
    section_header.into_ref().is_valid_block()?;

    let interface_description = buf_mut
        .take_obj_front::<InterfaceDescription>()
        .expect("Data cannot be read as an interface description block!");
    interface_description.into_ref().is_valid_block()?;
    Ok(buf_mut)
}

// Parse a simple packet block.
// Returns (unconsumed left-over `buf_mut`, packet, packet size).
fn parse_simple_packet<'a>(
    mut buf_mut: &'a mut &'a [u8],
) -> Result<(&'a mut &'a [u8], &'a [u8], usize), Error> {
    let simple_packet_slice = buf_mut.take_obj_front::<SimplePacket>().unwrap();
    let simple_packet = simple_packet_slice.into_ref();
    simple_packet.is_valid_block()?;

    let packet = buf_mut.take_front(with_padding(simple_packet.packet_length) as usize).unwrap();
    // The trailing total length of each simple packet block must equal the one at the start.
    let simple_packet_trailer = buf_mut.take_obj_front::<SimplePacketTrailer>().unwrap();
    let tot_length = simple_packet.tot_length;
    let tot_length2 = simple_packet_trailer.tot_length2;
    assert_eq!(tot_length, tot_length2);

    Ok((buf_mut, packet, simple_packet.packet_length as usize))
}

// For `pcapng_packet_headers_test`, some simple representations of Ethernet, IPv4 and UDP headers
// are used. Not all fields in the headers are verified by the test.
#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
struct SimpleEthernet {
    _dst_src_macs: [u8; 12],
    pub(crate) ethtype: u16,
}

#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
struct SimpleIpv4 {
    pub(crate) version_ihl: u8,
    _dscp_ecn: u8,
    pub(crate) total_length: u16,
    _id_flags_frag_offset: u32,
    _ttl: u8,
    pub(crate) protocol: u8,
    _checksum: u16,
    pub(crate) src_addr: [u8; 4],
    pub(crate) dst_addr: [u8; 4],
}

#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
struct SimpleUdp {
    pub(crate) src_port: u16,
    pub(crate) dst_port: u16,
    pub(crate) length: u16,
    _checksum: u16,
}

// True if `packet` is a UDP packet sent by `pcapng_packet_headers_test`
fn is_udp_packet(mut packet: &mut &[u8]) -> bool {
    let ethernet = packet.take_obj_front::<SimpleEthernet>().unwrap();
    // IPv4 ethertype.
    if ethernet.ethtype != 8 {
        return false;
    }

    let ipv4 = packet.take_obj_front::<SimpleIpv4>().unwrap();
    let src_addr = Ipv4Addr::from(ipv4.src_addr);
    let dst_addr = Ipv4Addr::from(ipv4.dst_addr);
    if src_addr != EndpointType::TX.default_socket_addr(IpVersion::V4).ip()
        || dst_addr != EndpointType::RX.default_socket_addr(IpVersion::V4).ip()
    {
        return false;
    }

    // Version 4, IHL 5.
    if ipv4.version_ihl != 0x45
        || u16::from_be(ipv4.total_length) as usize != IPV4_LENGTH + UDP_LENGTH + PAYLOAD_LENGTH
        || ipv4.protocol != 0x11
    // UDP protocol number.
    {
        return false;
    }

    let udp = packet.take_obj_front::<SimpleUdp>().unwrap();
    if u16::from_be(udp.src_port) != EndpointType::TX.default_port()
        || u16::from_be(udp.dst_port) != EndpointType::RX.default_port()
        || u16::from_be(udp.length) as usize != UDP_LENGTH + PAYLOAD_LENGTH
    {
        return false;
    }
    true
}

// When no packets are captured, the section header and interface description blocks should still
// be written.
fn pcapng_no_packets_test(env: &mut TestEnvironment) -> Result<(), Error> {
    let args = env
        .new_args(EndpointType::RX)
        .insert_packet_count(0)
        .insert_pcapng_dump_to_stdout()
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE);

    let output = env.run_test_case_no_packets(args.into())?;
    let dumpfile = read_default_dumpfile()?;
    assert_eq!(dumpfile.len(), SECTION_HEADER_LENGTH + INTERFACE_DESCRIPTION_LENGTH);
    assert_eq!(dumpfile, output.stdout);
    output.ok()?;

    let mut buf = dumpfile.as_slice();
    let buf_mut = consume_shb_idb(&mut buf)?;

    // All bytes are consumed.
    assert!(buf_mut.is_empty());

    Ok(())
}

// Tests that packet headers are captured and output in PCAPNG format.
// Only one correct packet is necessary for the test to pass.
// However the overall format of the dumpfile is still checked to be valid.
fn pcapng_packet_headers_test(env: &mut TestEnvironment) -> Result<(), Error> {
    let args = env
        .new_args(EndpointType::RX)
        .insert_packet_count(PACKET_COUNT)
        .insert_pcapng_dump_to_stdout()
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE);

    let socket_tx = UdpSocket::bind(EndpointType::TX.default_socket_addr(IpVersion::V4))?;
    let output = env.run_test_case(
        args.into(),
        send_udp_packets(
            socket_tx,
            EndpointType::RX.default_socket_addr(IpVersion::V4),
            PAYLOAD_LENGTH,
            PACKET_COUNT,
        ),
        DEFAULT_DUMPFILE,
    )?;

    let dumpfile = read_default_dumpfile()?;
    assert_eq!(dumpfile, output.stdout);
    output.ok()?;

    let mut buf = dumpfile.as_slice();
    let mut buf_mut = consume_shb_idb(&mut buf)?;

    let mut success = false;
    for _ in 0..PACKET_COUNT {
        let (new_buf_mut, mut packet, packet_length) = parse_simple_packet(buf_mut)?;
        buf_mut = new_buf_mut;
        let is_udp = is_udp_packet(&mut packet);
        success |= is_udp;
        if is_udp {
            // Verify the packet length recorded in PCAPNG is equal to the expected total.
            // Frame check sequence has been discarded.
            assert_eq!(packet_length, ETHERNET_LENGTH + IPV4_LENGTH + UDP_LENGTH + PAYLOAD_LENGTH);
        }
    }

    assert!((buf_mut.is_empty()));

    if success {
        Ok(())
    } else {
        Err(format_err!("Matching packet not found!"))
    }
}

// Tests that netdump is able to capture and dump malformed packets.
fn pcapng_fake_packets_test(env: &mut TestEnvironment) -> Result<(), Error> {
    const FAKE_DATA: &str = "this is a badly malformed pack";
    const FAKE_DATA_LENGTH: usize = 30;

    let args = env
        .new_args(EndpointType::TX) // Tx endpoint should work just as well.
        .insert_packet_count(PACKET_COUNT)
        .insert_pcapng_dump_to_stdout()
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE);
    let fake_ep = env.create_fake_endpoint()?;
    let send_fake_packets = async move {
        for _ in 0..(PACKET_COUNT) {
            let packet: Vec<u8> = FAKE_DATA.to_string().into_bytes();
            fake_ep.write(&mut packet.into_iter())?;
        }
        Ok(())
    };

    let output = env.run_test_case(args.into(), send_fake_packets, DEFAULT_DUMPFILE)?;
    let dumpfile = read_default_dumpfile()?;
    assert_eq!(dumpfile, output.stdout);
    output.ok()?;

    let mut buf = dumpfile.as_slice();
    let mut buf_mut = consume_shb_idb(&mut buf)?;

    for _ in 0..PACKET_COUNT {
        let (new_buf_mut, packet, packet_length) = parse_simple_packet(buf_mut)?;
        buf_mut = new_buf_mut;
        // Packet length is still the original length.
        assert_eq!(packet_length, FAKE_DATA_LENGTH);
        // But data should be padded to multiple of 4 octets.
        assert_eq!(
            String::from_utf8_lossy(packet),
            "this is a badly malformed pack\0\0".to_string()
        );
    }

    assert!((buf_mut.is_empty()));
    Ok(())
}
