// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_snoop::{PacketType, SnoopPacket, Timestamp},
    fidl_fuchsia_hardware_bluetooth::HciSynchronousProxy,
    fuchsia_async as fasync,
    fuchsia_zircon::{Channel, MessageBuf},
    futures::Stream,
    std::{
        fs::{File, OpenOptions},
        marker::Unpin,
        path::PathBuf,
        pin::Pin,
        task::{Context, Poll},
        time::{SystemTime, UNIX_EPOCH},
    },
};

/// A wrapper type for the bitmask representing flags in the snoop channel protocol.
struct HciFlags(u8);

impl HciFlags {
    const IS_RECEIVED: u8 = 0b100;
    const PACKET_TYPE_CMD: u8 = 0b00;
    const PACKET_TYPE_EVENT: u8 = 0b01;
    // Included for completeness. Used in tests
    #[allow(dead_code)]
    const PACKET_TYPE_DATA: u8 = 0b10;

    /// Does the packet represent an HCI event sent from the controller to the host?
    fn is_received(&self) -> bool {
        self.0 & HciFlags::IS_RECEIVED == HciFlags::IS_RECEIVED
    }
    /// Returns the packet type.
    fn hci_packet_type(&self) -> PacketType {
        match self.0 & 0b11 {
            HciFlags::PACKET_TYPE_CMD => PacketType::Cmd,
            HciFlags::PACKET_TYPE_EVENT => PacketType::Event,
            _ => PacketType::Data,
        }
    }
}

/// A Snooper provides a `Stream` associated with the snoop channel for a single HCI device. This
/// stream can be polled for `SnoopPacket`s coming off the channel.
pub(crate) struct Snooper {
    pub device_name: String,
    pub device_path: PathBuf,
    pub chan: fuchsia_async::Channel,
}

impl Snooper {
    /// Create a new snooper from a device path. This opens a new snoop channel, returning an error
    /// if the devices doesn't exist or the channel cannot be created.
    #[allow(dead_code)] // used in future
    pub fn new(device_path: PathBuf) -> Result<Snooper, Error> {
        let hci_device = OpenOptions::new().read(true).write(true).open(&device_path)?;
        let channel = open_snoop_channel(&hci_device)?;
        Snooper::from_channel(channel, device_path)
    }

    /// Take a channel and wrap it in a `Snooper`.
    #[allow(dead_code)] // used in tests
    pub fn from_channel(chan: Channel, device_path: PathBuf) -> Result<Snooper, Error> {
        let chan = fasync::Channel::from_channel(chan)?;

        let device_name = device_path
            .file_name()
            .ok_or(format_err!("A device has no name"))?
            .to_string_lossy()
            .into_owned();
        Ok(Snooper { device_name, device_path, chan })
    }

    /// Parse a raw byte buffer and return a SnoopPacket. Returns `None` if the buffer does not
    /// contain a valid packet.
    pub fn build_pkt(buf: MessageBuf) -> Option<SnoopPacket> {
        let duration = SystemTime::now().duration_since(UNIX_EPOCH).expect("Time went backwards");

        if buf.bytes().is_empty() {
            return None;
        }
        let flags = HciFlags(buf.bytes()[0]);

        let payload = buf.bytes()[1..].to_vec();
        let pkt = SnoopPacket {
            is_received: flags.is_received(),
            type_: flags.hci_packet_type(),
            timestamp: Timestamp {
                subsec_nanos: duration.subsec_nanos(),
                seconds: duration.as_secs(),
            },
            original_len: payload.len() as u32,
            payload,
        };

        Some(pkt)
    }
}

impl Unpin for Snooper {}
impl Stream for Snooper {
    type Item = (String, SnoopPacket);

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut buf = MessageBuf::new();
        match self.chan.recv_from(cx, &mut buf) {
            Poll::Ready(_t) => {
                let item = Snooper::build_pkt(buf).map(|pkt| (self.device_name.clone(), pkt));
                Poll::Ready(item)
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

// TODO (belgum) use asynchronous client
pub fn open_snoop_channel(device: &File) -> Result<Channel, Error> {
    let hci_channel = fdio::clone_channel(device)?;
    let mut interface = HciSynchronousProxy::new(hci_channel);
    let (ours, theirs) = Channel::create()?;
    interface.open_snoop_channel(theirs)?;
    Ok(ours)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_zircon::Channel,
        futures::StreamExt,
        std::{path::PathBuf, task::Poll},
    };

    #[test]
    fn test_from_channel() {
        let _exec = fasync::Executor::new();
        let (channel, _) = Channel::create().unwrap();
        let snooper = Snooper::from_channel(channel, PathBuf::from("/a/b/c")).unwrap();
        assert_eq!(snooper.device_name, "c");
        assert_eq!(snooper.device_path, PathBuf::from("/a/b/c"));
    }

    #[test]
    fn test_build_pkt() {
        let flags = HciFlags::IS_RECEIVED;
        let buf = MessageBuf::new_with(vec![flags], vec![]);
        let pkt = Snooper::build_pkt(buf).unwrap();
        assert!(pkt.is_received);
        assert!(pkt.payload.is_empty());
        assert!(pkt.timestamp.seconds > 0);
        assert_eq!(pkt.type_, PacketType::Cmd);

        let flags = HciFlags::PACKET_TYPE_DATA;
        let buf = MessageBuf::new_with(vec![flags, 0, 1, 2], vec![]);
        let pkt = Snooper::build_pkt(buf).unwrap();
        assert!(!pkt.is_received);
        assert_eq!(pkt.payload, vec![0, 1, 2]);
        assert_eq!(pkt.type_, PacketType::Data);
    }

    #[test]
    fn test_snoop_stream() {
        let mut exec = fasync::Executor::new().unwrap();
        let (tx, rx) = Channel::create().unwrap();
        let mut snooper = Snooper::from_channel(rx, PathBuf::from("/a/b/c")).unwrap();
        let flags = HciFlags::IS_RECEIVED | HciFlags::PACKET_TYPE_EVENT;
        tx.write(&[flags, 0, 1, 2], &mut vec![]).unwrap();
        tx.write(&[0, 3, 4, 5], &mut vec![]).unwrap();

        let item_1 = exec.run_until_stalled(&mut snooper.next());
        let item_2 = exec.run_until_stalled(&mut snooper.next());
        assert!(item_1.is_ready());
        assert!(item_2.is_ready());
        match (item_1, item_2) {
            (Poll::Ready(item_1), Poll::Ready(item_2)) => {
                assert_eq!(item_1.unwrap().1.payload, vec![0, 1, 2]);
                assert_eq!(item_2.unwrap().1.payload, vec![3, 4, 5]);
            }
            _ => panic!("failed to build both packets 1 and 2 from snoop stream"),
        }

        let item_3 = exec.run_until_stalled(&mut snooper.next());
        assert!(item_3.is_pending());
    }
}
