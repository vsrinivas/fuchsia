// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::{Duration, SystemTime, UNIX_EPOCH};

use async;
use byteorder::{BigEndian, WriteBytesExt};
use failure::Error;
use futures::{task, Async, Poll, Stream};

use zircon::{Channel, MessageBuf};

const SNOOP_RECIEVED: u8 = 0x01;
const SNOOP_DATA: u8 = 0x02;
const PCAP_CMD: u8 = 0x01;
const PCAP_DATA: u8 = 0x02;
const PCAP_EVENT: u8 = 0x04;

#[derive(Debug)]
pub enum Hci {
    Cmd,
    Event,
    Data,
}

#[derive(Debug)]
pub enum Format {
    Btsnoop,
    Pcap,
}

#[derive(Debug)]
pub struct SnooperPacket {
    pub len: u32,
    pub is_recieved: bool,
    pub hci_flag: Hci,
    pub timestamp: Duration,
    pub payload: Vec<u8>,
}

impl SnooperPacket {
    pub fn to_btsnoop_fmt(self) -> Vec<u8> {
        let mut flag = 0x0;
        if self.is_recieved {
            flag |= SNOOP_RECIEVED;
        }
        match self.hci_flag {
            Hci::Cmd => {}
            Hci::Data => {
                flag |= SNOOP_DATA;
            }
            Hci::Event => {
                flag |= SNOOP_DATA;
            }
        }

        let mut wtr = vec![];
        wtr.write_u32::<BigEndian>(self.len).unwrap();
        wtr.write_u32::<BigEndian>(self.len).unwrap();
        wtr.write_u32::<BigEndian>(flag as u32).unwrap();
        wtr.write_u32::<BigEndian>(0).unwrap(); // drops
        wtr.write_i64::<BigEndian>(self.timestamp.as_secs() as i64)
            .unwrap();
        wtr.extend(&self.payload);
        wtr
    }

    pub fn to_pcap_fmt(self) -> Vec<u8> {
        let mut wtr = vec![];
        wtr.write_u32::<BigEndian>(self.timestamp.as_secs() as u32)
            .unwrap();
        wtr.write_u32::<BigEndian>(self.timestamp.subsec_nanos() / 1_000_000 as u32)
            .unwrap();
        wtr.write_u32::<BigEndian>((self.len + 5) as u32).unwrap(); // len(payload) + 5 octets
        wtr.write_u32::<BigEndian>((self.len + 5) as u32).unwrap();
        wtr.write_u32::<BigEndian>(self.is_recieved as u32).unwrap();
        match self.hci_flag {
            Hci::Cmd => {
                wtr.write_u8(PCAP_CMD).unwrap();
            }
            Hci::Data => {
                wtr.write_u8(PCAP_DATA).unwrap();
            }
            Hci::Event => {
                wtr.write_u8(PCAP_EVENT).unwrap();
            }
        }
        wtr.extend(&self.payload);
        wtr
    }
}

pub struct Snooper {
    pub chan: async::Channel,
    pub buf: MessageBuf,
}

impl Snooper {
    pub fn new(snoop_chan: Channel) -> Snooper {
        Snooper {
            chan: async::Channel::from_channel(snoop_chan).unwrap(),
            buf: MessageBuf::new(),
        }
    }

    pub fn btsnoop_header() -> Vec<u8> {
        let mut wtr = vec![b'b', b't', b's', b'n', b'o', b'o', b'p', b'\0'];
        wtr.write_u32::<BigEndian>(1).unwrap();
        wtr.write_u32::<BigEndian>(1001).unwrap();
        wtr
    }

    pub fn pcap_header() -> Vec<u8> {
        let mut wtr = vec![];
        wtr.write_u32::<BigEndian>(0xa1b2c3d4).unwrap(); // Magic number
        wtr.write_u16::<BigEndian>(2).unwrap(); // Major Version
        wtr.write_u16::<BigEndian>(4).unwrap(); // Minor Version
        wtr.write_i32::<BigEndian>(0).unwrap(); // Timezone: GMT
        wtr.write_u32::<BigEndian>(0).unwrap(); // Sigfigs
        wtr.write_u32::<BigEndian>(65535).unwrap(); // Max packet length
        wtr.write_u32::<BigEndian>(201).unwrap(); // Protocol: BLUETOOTH_HCI_H4_WITH_PHDR
        wtr
    }

    pub fn build_pkt(&mut self) -> Option<<Snooper as Stream>::Item> {
        let duration = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("Time went backwards");

        let mut flags = self.buf.bytes()[0];
        let is_recieved = (flags & 0x04) == 0x04;
        if is_recieved {
            flags -= 0x04;
        }
        let hci_flag = match flags {
            flags if (flags == 0x00) => Hci::Cmd,
            flags if (flags == 0x01) => Hci::Event,
            flags if (flags == 0x02) => Hci::Data,
            _ => Hci::Cmd,
        };

        let pkt = SnooperPacket {
            len: self.buf.bytes()[1..].len() as u32,
            is_recieved,
            hci_flag,
            timestamp: duration,
            payload: self.buf.bytes()[1..].to_vec(),
        };

        Some(pkt)
    }
}

impl Stream for Snooper {
    type Item = SnooperPacket;
    type Error = Error;

    fn poll_next(&mut self, cx: &mut task::Context) -> Poll<Option<Self::Item>, Self::Error> {
        match self.chan.recv_from(&mut self.buf, cx) {
            Ok(fut) => match fut {
                Async::Ready(_t) => Ok(Async::Ready(self.build_pkt())),
                Async::Pending => Ok(Async::Pending),
            },
            Err(e) => Err(e.into()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn write_packet() {
        let mut exec = Executor::new().unwrap();
        let (p1, _p2) = Channel::create().unwrap();
        let mut snooper = Snooper::new(Channel::from(p1));

        let mut out_vec: Vec<u8> = vec![];
        let incoming_flag = 0x02;
        let pkt = zircon::MessageBuf::new_with(vec![incoming_flag, b'T', b'e', b's', b't'], vec![]);

        out_vec.write(Snooper::header().as_slice());
        snooper.buf = pkt;
        out_vec.extend(snooper.write_snoop_pkt().unwrap().to_bytes().as_slice());

        let expected_vec_one = [
            b'b', b't', b's', b'n', b'o', b'o', b'p', b'\0', 0x00, 0x00, 0x00,
            0x01 /* version number */, 0x00, 0x00, 0x03,
            0xE9 /* data link type (H1: 1001) */, /* Record */ 0x00, 0x00, 0x00,
            0x04 /* original length ("Test") */, 0x00, 0x00, 0x00,
            0x04 /* included length ("Test") */, 0x00, 0x00, 0x00,
            0x02 /* packet flags: sent (0x00) | cmd (0x02) */, 0x00, 0x00, 0x00,
            0x00 /* cumulative drops */,
        ];
        // OMIT TIMESTAMP
        let expected_vec_two = [b'T', b'e', b's', b't'];
        assert_eq!(out_vec[..32], expected_vec_one);
        assert_eq!(out_vec[40..], expected_vec_two);
    }
}
