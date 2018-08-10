// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::{Duration, SystemTime, UNIX_EPOCH};

use async;
use byteorder::{BigEndian, WriteBytesExt};
use futures::{task, Poll, Stream};
use std::marker::Unpin;
use std::mem::PinMut;

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

    pub fn build_pkt(buf: MessageBuf) -> Option<<Snooper as Stream>::Item> {
        let duration = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("Time went backwards");

        let mut flags = buf.bytes()[0];
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
            len: buf.bytes()[1..].len() as u32,
            is_recieved,
            hci_flag,
            timestamp: duration,
            payload: buf.bytes()[1..].to_vec(),
        };

        Some(pkt)
    }
}

impl Unpin for Snooper {}

impl Stream for Snooper {
    type Item = SnooperPacket;

    fn poll_next(
        self: PinMut<Self>, cx: &mut task::Context,
    ) -> Poll<Option<Self::Item>> {
        let mut buf = MessageBuf::new();
        match self.chan.recv_from(&mut buf, cx) {
            Poll::Ready(_t) => Poll::Ready(Snooper::build_pkt(buf)),
            Poll::Pending => Poll::Pending,
        }
    }
}
