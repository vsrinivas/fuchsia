// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
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

use crate::types::{
    decode_opcode, parse_inquiry_result, EventPacketType, InquiryResult, StatusCode,
};

/// Default HCI device on the system.
/// TODO: consider supporting more than one HCI device.
const DEFAULT_DEVICE: &str = "/dev/class/bt-hci/000";

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
                    Poll::Ready(Some(EventPacket::new(buf.bytes())))
                }
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

/// Event packets from the HCI device.
#[derive(Debug)]
pub enum EventPacket {
    CommandComplete { opcode: u16, payload: Vec<u8> },
    CommandStatus { opcode: u16, status_code: StatusCode, payload: Vec<u8> },
    InquiryResult { results: Vec<InquiryResult>, payload: Vec<u8> },
    InquiryComplete { status_code: StatusCode, payload: Vec<u8> },
    Unknown { payload: Vec<u8> },
}

const HCI_COMMAND_COMPLETE_MIN: usize = 5;
const HCI_COMMAND_STATUS_MIN: usize = 6;
const HCI_INQUIRY_RESULT_MIN: usize = 3;
const HCI_INQUIRY_COMPLETE_MIN: usize = 3;

impl EventPacket {
    pub fn new(payload: &[u8]) -> EventPacket {
        assert!(!payload.is_empty(), "HCI Packet is empty");

        let payload = payload.to_vec();
        match EventPacketType::try_from(payload[0]) {
            Ok(EventPacketType::CommandComplete) => {
                if payload.len() < HCI_COMMAND_COMPLETE_MIN {
                    return EventPacket::Unknown { payload };
                }
                let opcode = decode_opcode(&payload[3..=4]);
                EventPacket::CommandComplete { opcode, payload }
            }
            Ok(EventPacketType::CommandStatus) => {
                if payload.len() < HCI_COMMAND_STATUS_MIN {
                    return EventPacket::Unknown { payload };
                }
                let opcode = decode_opcode(&payload[4..=5]);
                let status_code =
                    StatusCode::try_from(payload[2]).unwrap_or(StatusCode::UnknownCommand);
                EventPacket::CommandStatus { opcode, status_code, payload }
            }
            Ok(EventPacketType::InquiryResult) => {
                if payload.len() < HCI_INQUIRY_RESULT_MIN {
                    return EventPacket::Unknown { payload };
                }
                let results = parse_inquiry_result(&payload[..]);
                EventPacket::InquiryResult { results, payload }
            }
            Ok(EventPacketType::InquiryComplete) => {
                if payload.len() < HCI_INQUIRY_COMPLETE_MIN {
                    return EventPacket::Unknown { payload };
                }
                let status_code =
                    StatusCode::try_from(payload[2]).unwrap_or(StatusCode::UnknownCommand);
                EventPacket::InquiryComplete { status_code, payload }
            }
            Err(_) => EventPacket::Unknown { payload },
        }
    }

    /// Returns a human readable representation of the event.
    pub fn decode(&self) -> String {
        match self {
            EventPacket::CommandComplete { opcode, .. } => {
                format!("HCI_Command_Complete Opcode: {} Payload: {}", opcode, self)
            }
            EventPacket::CommandStatus { opcode, status_code, .. } => format!(
                "HCI_Command_Status Opcode: {} Status: {} Payload: {}",
                opcode,
                status_code.name(),
                self
            ),
            EventPacket::InquiryResult { results, .. } => {
                format!("HCI_Inquiry_Result results: {:?}", results)
            }
            EventPacket::InquiryComplete { status_code, .. } => {
                format!("HCI_Inquiry_Complete Status: {}", status_code.name())
            }
            EventPacket::Unknown { payload } => {
                format!("Unknown Event: 0x{} Payload: {}", hex::encode(&[payload[0]]), self)
            }
        }
    }

    pub fn payload(&self) -> &Vec<u8> {
        match self {
            EventPacket::CommandComplete { payload, .. } => payload,
            EventPacket::CommandStatus { payload, .. } => payload,
            EventPacket::InquiryResult { payload, .. } => payload,
            EventPacket::InquiryComplete { payload, .. } => payload,
            EventPacket::Unknown { payload, .. } => payload,
        }
    }

    pub fn opcode(&self) -> Option<u16> {
        match self {
            EventPacket::CommandComplete { opcode, .. } => Some(*opcode),
            EventPacket::CommandStatus { opcode, .. } => Some(*opcode),
            _ => None,
        }
    }
}

impl fmt::Display for EventPacket {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", hex::encode(self.payload()))
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
                    pkt.payload(),
                    &vec![
                        0x0e, // HCI_COMMAND_COMPLETE
                        0x04, 0x01, //
                        0x03, 0x0c, // opcode (little endian)
                        0x00  // payload len
                    ]
                );
                let opcode = pkt.opcode();
                assert_eq!(opcode, Some(0x0c03));
            }
            _ => panic!("failed to decode packets from stream"),
        }

        let pending_item = exec.run_until_stalled(&mut command_channel.next());
        assert!(pending_item.is_pending());
    }

    #[test]
    fn test_event_packet_decode_cmd_complete() {
        let pkt = EventPacket::new(&[
            0x0e, 0x04, // HCI_Command_Complete
            0x01, //
            0x03, 0x0c, // opcode (little endian)
            0x00, // payload len
        ]);
        let opcode = pkt.opcode();
        assert_eq!(opcode, Some(0x0c03));
        let pretty_string = pkt.decode();
        assert_eq!(
            pretty_string,
            "HCI_Command_Complete Opcode: 3075 Payload: 0e0401030c00".to_string()
        );
    }

    #[test]
    fn test_event_packet_decode_inquiry() {
        let pkt = EventPacket::new(&[
            0x02, 0x0f, // HCI_Inquiry_Result
            0x01, // results count
            0x7c, 0x48, 0xc6, 0x8b, 0x42, 0x74, // br_addr
            0x01, // page_scan_repetition_mode:
            0x00, 0x00, // reserved
            0x0c, 0x02, 0x7a, // class of device
            0x45, 0x25, // clock offset
        ]);
        match pkt {
            EventPacket::InquiryResult { results, .. } => {
                assert!(results.len() == 1);
                let result = &results[0];
                assert_eq!(&result.br_addr, &[0x7c, 0x48, 0xc6, 0x8b, 0x42, 0x74]);
                assert_eq!(result.page_scan_repetition_mode, 1);
                assert_eq!(&result.class_of_device, &[0x0c, 0x02, 0x7a]);
                assert_eq!(result.clockoffset, 9541);
            }
            _ => assert!(false, "packet not an inquiry result"),
        }
    }

    #[test]
    fn test_event_packet_decode_inquiry_complete() {
        let pkt = EventPacket::new(&[
            0x01, 0x0f, // HCI_Inquiry_Complete
            0x00,
        ]);
        match pkt {
            EventPacket::InquiryComplete { status_code, .. } => {
                assert_eq!(status_code, StatusCode::Success);
            }
            _ => assert!(false, "packet not an inquiry complete"),
        }
    }

    #[test]
    fn test_event_packet_decode_unhandled() {
        let pkt = EventPacket::new(&[0x0e]);
        let e = pkt.decode();
        assert_eq!(format!("{}", e), "Unknown Event: 0x0e Payload: 0e".to_string());
    }
}
