// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::types::{decode_opcode, StatusCode},
    anyhow::Error,
    fidl_fuchsia_hardware_bluetooth::HciProxy,
    fuchsia_async as fasync,
    fuchsia_zircon::{Channel, MessageBuf},
    futures::Stream,
    std::convert::TryFrom,
    std::{
        fmt,
        fs::OpenOptions,
        marker::Unpin,
        path::PathBuf,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Default HCI device on the system.
/// TODO: consider supporting more than one HCI device.
const DEFAULT_DEVICE: &str = "/dev/class/bt-hci/000";

/// Command completed event.
const HCI_COMMAND_COMPLETE: u8 = 0x0E;
/// Command status event.
const HCI_COMMAND_STATUS: u8 = 0x0F;

/// A CommandChannel provides a `Stream` associated with the control channel for a single HCI device.
/// This stream can be polled for `EventPacket`s coming off the channel.
pub struct CommandChannel {
    pub chan: fuchsia_async::Channel,
}

impl CommandChannel {
    /// Create a new CommandChannel from a device path. This opens a new command channel, returning
    /// an error if the device doesn't exist or the channel cannot be created.
    pub fn new(device_path: PathBuf) -> Result<CommandChannel, Error> {
        let channel = open_command_channel(device_path)?;
        CommandChannel::from_channel(channel)
    }

    /// Take a channel and wrap it in a `CommandChannel`.
    fn from_channel(chan: Channel) -> Result<CommandChannel, Error> {
        let chan = fasync::Channel::from_channel(chan)?;

        Ok(CommandChannel { chan })
    }

    pub fn send_command_packet(&self, buf: &[u8]) -> Result<(), Error> {
        self.chan.write(buf, &mut vec![])?;
        Ok(())
    }
}

impl Unpin for CommandChannel {}
impl Stream for CommandChannel {
    type Item = EventPacket;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut buf = MessageBuf::new();
        match self.chan.recv_from(cx, &mut buf) {
            Poll::Ready(_t) => {
                if buf.bytes().is_empty() {
                    Poll::Ready(None)
                } else {
                    Poll::Ready(Some(EventPacket { payload: buf.bytes().to_vec() }))
                }
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

#[derive(Debug)]
pub struct EventPacket {
    /// Payload sent over the HCI.
    pub payload: Vec<u8>,
}

impl EventPacket {
    /// Decode the event packet. Returns a tuple of a human readable representation and
    /// the associated op code if available from a Complete or Status event packet.
    pub fn decode(&self) -> Result<(String, Option<u16>), Error> {
        if self.payload.is_empty() {
            return Err(Error::msg("Packet contains no payload"));
        }

        if self.payload[0] == HCI_COMMAND_COMPLETE {
            if self.payload.len() < 5 {
                return Err(Error::msg("Incomplete Complete Packet"));
            }
            let opcode = decode_opcode(&self.payload[3..=4]);
            return Ok((
                format!("Event: 0x0e (HCI_COMMAND_COMPLETE) Opcode: {} Payload: {}", opcode, self),
                Some(opcode),
            ));
        } else if self.payload[0] == HCI_COMMAND_STATUS {
            if self.payload.len() < 6 {
                return Err(Error::msg("Incomplete Status Packet"));
            }
            let opcode = decode_opcode(&self.payload[4..=5]);
            let status_code = StatusCode::try_from(self.payload[3]).map_err(|e| Error::from(e))?;
            return Ok((
                format!(
                    "Event: 0x0f (HCI_COMMAND_STATUS) Opcode: {} Status: {} Payload: {}",
                    opcode,
                    status_code.name(),
                    self
                ),
                Some(opcode),
            ));
        }
        Ok((format!("Event: 0x{} Payload: {}", hex::encode(&[self.payload[0]]), self), None))
    }
}

impl fmt::Display for EventPacket {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", hex::encode(&self.payload))
    }
}

fn open_command_channel(device_path: PathBuf) -> Result<Channel, Error> {
    let hci_device = OpenOptions::new().read(true).write(true).open(&device_path)?;
    let hci_channel = fasync::Channel::from_channel(fdio::clone_channel(&hci_device)?)?;
    let interface = HciProxy::new(hci_channel);
    let (ours, theirs) = Channel::create()?;
    interface.open_command_channel(theirs)?;
    Ok(ours)
}

pub fn open_default_device() -> Result<CommandChannel, Error> {
    CommandChannel::new(DEFAULT_DEVICE.into())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_async as fasync, fuchsia_zircon::Channel, futures::StreamExt,
        std::task::Poll,
    };

    #[test]
    fn test_from_channel() {
        let _exec = fasync::Executor::new();
        let (channel, _) = Channel::create().unwrap();
        let _command_channel = CommandChannel::from_channel(channel).unwrap();
    }

    #[test]
    fn test_command_channel() {
        let mut exec = fasync::Executor::new().unwrap();
        let (remote, local) = Channel::create().unwrap();
        let command_channel = CommandChannel::from_channel(local).unwrap();

        pin_utils::pin_mut!(command_channel);

        let _ = command_channel.send_command_packet(&[0x03, 0x0c, 0x00]).expect("unable to send");

        let mut buf = MessageBuf::new();
        let _ = remote.read(&mut buf).expect("unable to read");
        assert_eq!(buf.bytes(), &[0x03, 0x0c, 0x00]);

        remote.write(&[0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00], &mut vec![]).unwrap();

        let packet = exec.run_until_stalled(&mut command_channel.next());
        assert!(packet.is_ready());
        match packet {
            Poll::Ready(packet) => {
                let pkt = packet.expect("no packet");
                assert_eq!(
                    pkt.payload,
                    vec![
                        0x0e, // HCI_COMMAND_COMPLETE
                        0x04, 0x01, //
                        0x03, 0x0c, // opcode (little endian)
                        0x00  // payload len
                    ]
                );
                let (_pretty_string, opcode) = pkt.decode().expect("unable to decode");
                assert_eq!(opcode, Some(0x0c03));
            }
            _ => panic!("failed to decode packets from stream"),
        }

        let pending_item = exec.run_until_stalled(&mut command_channel.next());
        assert!(pending_item.is_pending());
    }

    #[test]
    fn test_event_packet_decode() {
        let pkt = EventPacket {
            payload: vec![
                0x0e, // HCI_COMMAND_COMPLETE
                0x04, 0x01, //
                0x03, 0x0c, // opcode (little endian)
                0x00, // payload len
            ],
        };
        let (pretty_string, opcode) = pkt.decode().expect("unable to decode");
        assert_eq!(opcode, Some(0x0c03));
        assert_eq!(
            pretty_string,
            "Event: 0x0e (HCI_COMMAND_COMPLETE) Opcode: 3075 Payload: 0e0401030c00".to_string()
        );
    }

    #[test]
    fn test_event_packet_decode_failure() {
        let pkt = EventPacket { payload: vec![0x0e] };
        let e = pkt.decode().expect_err("unable to decode");
        assert_eq!(format!("{}", e), "Incomplete Complete Packet".to_string());
    }
}
