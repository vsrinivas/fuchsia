// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation that extends the generic control protocol to comply with LCP.

use {
    crate::ppp::{
        self, ControlProtocol as PppControlProtocol, FrameError, FrameTransmitter, ProtocolError,
        ProtocolState, CODE_CODE_REJECT, CODE_CONFIGURE_ACK, CODE_CONFIGURE_NAK,
        CODE_CONFIGURE_REJECT, CODE_CONFIGURE_REQUEST, CODE_TERMINATE_ACK, CODE_TERMINATE_REQUEST,
        DEFAULT_MAX_FRAME, PROTOCOL_LINK_CONTROL,
    },
    packet::{Buf, GrowBuffer, InnerPacketBuilder, ParsablePacket, ParseBuffer, Serializer},
    ppp_packet::{
        link,
        records::options::{Options, OptionsSerializer},
        CodeRejectPacket, ConfigurationPacket, ControlProtocolPacket, ControlProtocolPacketBuilder,
        EchoDiscardPacket, EchoDiscardPacketBuilder, PppPacketBuilder, ProtocolRejectPacket,
        ProtocolRejectPacketBuilder, TerminationPacket,
    },
};

/// The code byte pattern used in a Protocol-Rej packet.
const CODE_PROTOCOL_REJECT: u8 = 8;
/// The code byte pattern used in an Echo-Request packet.
const CODE_ECHO_REQUEST: u8 = 9;
/// The code byte pattern used in an Echo-Reply packet.
const CODE_ECHO_REPLY: u8 = 10;
/// The code byte pattern used in an Discard-Request packet.
const CODE_DISCARD_REQUEST: u8 = 11;

/// An implementation of the PPP Link Control Protocol.
#[derive(Debug, Copy, Clone)]
pub struct ControlProtocol;

impl ppp::ControlProtocol for ControlProtocol {
    type Option = link::ControlOption;
    const PROTOCOL_IDENTIFIER: u16 = PROTOCOL_LINK_CONTROL;

    fn unacceptable_options(received: &[link::ControlOption]) -> Vec<link::ControlOption> {
        received
            .iter()
            .filter(|option| match option {
                link::ControlOption::MagicNumber(magic_number) => *magic_number == 0,
                _ => true,
            })
            .cloned()
            .collect::<Vec<_>>()
    }

    fn parse_options(buf: &[u8]) -> Option<Vec<link::ControlOption>> {
        Options::<_, link::ControlOptionsImpl>::parse(buf)
            .ok()
            .map(|options| options.iter().collect())
    }

    fn serialize_options(options: &[link::ControlOption]) -> ::packet::Buf<Vec<u8>> {
        crate::flatten_either(
            OptionsSerializer::<link::ControlOptionsImpl, link::ControlOption, _>::new(
                options.iter(),
            )
            .into_serializer()
            .serialize_vec_outer()
            .ok()
            .unwrap(),
        )
    }
}

/// Update the current LCP state given the current time by performing restarts.
pub async fn update<T>(
    resumable_state: ProtocolState<ControlProtocol>,
    transmitter: &T,
    time: std::time::Instant,
) -> Result<ProtocolState<ControlProtocol>, ProtocolError<ControlProtocol>>
where
    T: FrameTransmitter,
{
    resumable_state.restart(transmitter, time).await
}

/// Process an incoming LCP packet, driving the state machine forward, producing a new state or an
/// error.
pub async fn receive<T, B>(
    resumable_state: ProtocolState<ControlProtocol>,
    transmitter: &T,
    mut buf: ::packet::Buf<B>,
    time: std::time::Instant,
) -> Result<ProtocolState<ControlProtocol>, ProtocolError<ControlProtocol>>
where
    T: FrameTransmitter,
    B: AsRef<[u8]> + AsMut<[u8]>,
{
    let control_packet = if let Ok(control_packet) = buf.parse::<ControlProtocolPacket<_>>() {
        control_packet
    } else {
        return Ok(resumable_state);
    };
    let code = control_packet.code();
    let identifier = control_packet.identifier();

    match code {
        CODE_CONFIGURE_REQUEST
        | CODE_CONFIGURE_ACK
        | CODE_CONFIGURE_NAK
        | CODE_CONFIGURE_REJECT => {
            if buf.parse::<ConfigurationPacket<_>>().is_err() {
                return Ok(resumable_state);
            }
            let options = if let Some(options) = ControlProtocol::parse_options(buf.as_ref()) {
                options
            } else {
                return Ok(resumable_state);
            };
            match code {
                CODE_CONFIGURE_REQUEST => {
                    resumable_state.rx_configure_req(transmitter, &options, identifier, time).await
                }
                CODE_CONFIGURE_ACK => {
                    resumable_state.rx_configure_ack(transmitter, &options, identifier, time).await
                }
                CODE_CONFIGURE_NAK | CODE_CONFIGURE_REJECT => {
                    resumable_state.rx_configure_rej(transmitter, &options, identifier, time).await
                }
                _ => unreachable!(),
            }
        }
        CODE_TERMINATE_REQUEST | CODE_TERMINATE_ACK => {
            if buf.parse::<TerminationPacket<_>>().is_err() {
                return Ok(resumable_state);
            }
            resumable_state.rx_terminate_req(transmitter, identifier).await
        }
        CODE_CODE_REJECT => {
            if buf.parse::<CodeRejectPacket<_>>().is_err() {
                return Ok(resumable_state);
            }
            Err(ProtocolError::FatalCodeRej(buf.as_ref().to_vec()))
        }
        CODE_PROTOCOL_REJECT => {
            let protocol_rej_packet =
                if let Ok(protocol_rej_packet) = buf.parse::<ProtocolRejectPacket<_>>() {
                    protocol_rej_packet
                } else {
                    return Ok(resumable_state);
                };
            Err(ProtocolError::FatalProtocolRej(protocol_rej_packet.rejected_protocol()))
        }
        CODE_DISCARD_REQUEST | CODE_ECHO_REPLY => {
            let echo_discard_packet =
                if let Ok(echo_discard_packet) = buf.parse::<EchoDiscardPacket<_>>() {
                    echo_discard_packet
                } else {
                    return Ok(resumable_state);
                };
            let _magic_number = echo_discard_packet.magic_number();
            Ok(resumable_state)
        }
        CODE_ECHO_REQUEST => {
            if buf.parse::<EchoDiscardPacket<_>>().is_err() {
                return Ok(resumable_state);
            }
            if let ProtocolState::Opened(ref opened) = &resumable_state {
                let magic_number = opened
                    .local_options()
                    .iter()
                    .filter_map(|option| match option {
                        link::ControlOption::MagicNumber(magic_number) => Some(*magic_number),
                        _ => None,
                    })
                    .next()
                    .unwrap_or(0);
                tx_echo_reply(transmitter, magic_number, identifier).await?;
            }
            Ok(resumable_state)
        }
        _ => {
            let metadata = control_packet.parse_metadata();
            buf.undo_parse(metadata);
            ppp::tx_code_rej::<ControlProtocol, _, _>(transmitter, buf, identifier).await?;
            Ok(resumable_state)
        }
    }
}

/// Serialize and transmit a Protocol-Rej packet with the provided rejected protocol and protocol
/// packet body.
pub async fn tx_protocol_rej<T, B>(
    transmitter: &T,
    buf: B,
    rejected_protocol: u16,
    identifier: u8,
) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    B: ::packet::BufferMut,
{
    let frame = buf
        .encapsulate(ProtocolRejectPacketBuilder::new(rejected_protocol))
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_PROTOCOL_REJECT, identifier))
        .encapsulate(PppPacketBuilder::new(PROTOCOL_LINK_CONTROL))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    // truncate the end bytes
    let frame: &[u8] = frame.as_ref();
    let bound = std::cmp::min(frame.len(), DEFAULT_MAX_FRAME as usize);
    transmitter.tx_frame(&frame[..bound]).await
}

/// Serialize and transmit an Echo-Reply packet with the provided magic number and identifier.
async fn tx_echo_reply<T>(
    transmitter: &T,
    magic_number: u32,
    identifier: u8,
) -> Result<(), FrameError>
where
    T: FrameTransmitter,
{
    let frame = Buf::new(&mut [], ..)
        .encapsulate(EchoDiscardPacketBuilder::new(magic_number))
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_ECHO_REPLY, identifier))
        .encapsulate(PppPacketBuilder::new(PROTOCOL_LINK_CONTROL))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    transmitter.tx_frame(frame.as_ref()).await
}
