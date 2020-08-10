// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::{Time, TimeoutExt},
    fuchsia_bluetooth::types::Channel,
    fuchsia_syslog::{fx_log_info, fx_vlog},
    fuchsia_zircon::Duration,
    futures::{future, future::Ready, stream::FilterMap, Stream, StreamExt},
    std::convert::TryFrom,
};

#[cfg(test)]
mod tests;

mod types;

use crate::{
    avctp::{
        Command as AvctpCommand, CommandStream as AvctpCommandStream, Header as AvctpHeader,
        Packet as AvctpPacket, Peer as AvctpPeer,
    },
    Decodable, Encodable, Error, Result,
};

use self::types::BT_SIG_COMPANY_ID;

pub use self::types::{CommandType, Header, OpCode, PacketType, ResponseType, SubunitType};

pub type CommandStream = FilterMap<
    AvctpCommandStream,
    Ready<Option<Result<Command>>>,
    fn(Result<AvctpCommand>) -> Ready<Option<Result<Command>>>,
>;

/// Represents a received AVC Command from a `Peer`.
#[derive(Debug)]
pub struct Command {
    inner: AvctpCommand,
    avc_header: Header,
}

impl Command {
    pub fn avctp_header(&self) -> &AvctpHeader {
        self.inner.header()
    }

    pub fn avc_header(&self) -> &Header {
        &self.avc_header
    }

    pub fn body(&self) -> &[u8] {
        &self.inner.body()[self.avc_header.encoded_len()..]
    }

    pub fn send_response(&self, response_type: ResponseType, body: &[u8]) -> Result<()> {
        let response_header = self.avc_header.create_response(response_type)?;
        let mut rbuf = vec![0 as u8; response_header.encoded_len()];
        response_header.encode(&mut rbuf[..])?;
        if body.len() > 0 {
            rbuf.extend_from_slice(body);
        }
        self.inner.send_response(rbuf.as_slice())
    }

    pub fn is_vendor_dependent(&self) -> bool {
        self.avc_header.op_code() == &OpCode::VendorDependent
    }
}

impl TryFrom<Result<AvctpCommand>> for Command {
    type Error = Error;

    fn try_from(value: Result<AvctpCommand>) -> Result<Command> {
        let inner = match value {
            Err(e) => return Err(e),
            Ok(inner) => inner,
        };
        let avc_header = match Header::decode(inner.body()) {
            Err(e) => return Err(e),
            Ok(head) => head,
        };
        Ok(Command { inner, avc_header })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct CommandResponse(pub ResponseType, pub Vec<u8>);

impl CommandResponse {
    pub fn response_type(&self) -> ResponseType {
        return self.0;
    }

    pub fn response(&self) -> &[u8] {
        return self.1.as_slice();
    }
}

impl TryFrom<AvctpPacket> for CommandResponse {
    type Error = Error;

    fn try_from(value: AvctpPacket) -> Result<CommandResponse> {
        let buf = value.body();
        let avc_header = Header::decode(buf)?;
        let body = buf[avc_header.encoded_len()..].to_vec();
        if let PacketType::Response(response_type) = avc_header.packet_type() {
            Ok(CommandResponse(response_type, body))
        } else {
            Err(Error::InvalidHeader)
        }
    }
}

/// Represents a peer connection to a remote device that uses the AV\C protocol over AVCTP encoded
/// L2CAP socket. Primarily used for the the control channel in AVRCP.
#[derive(Debug)]
pub struct Peer {
    /// The encapsulated AVCTP peer connection to the remote peer.
    inner: AvctpPeer,
}

impl Peer {
    /// Create a new peer object from a established L2CAP socket with the peer.
    pub fn new(channel: Channel) -> Self {
        Self { inner: AvctpPeer::new(channel) }
    }

    /// Decodes AV\C commands received over encapsulated AVCTP socket. Invalid AV|C commands are
    /// converted to errors.
    /// Note: Unit info and subunit info are responded to directly and swallowed since they return a
    /// static response.
    fn filter_internal_responses(
        avct_command_result: Result<AvctpCommand>,
    ) -> Option<Result<Command>> {
        let cmd = match Command::try_from(avct_command_result) {
            Ok(cmd) => cmd,
            Err(e) => return Some(Err(e)),
        };

        // Handle some early return short cutting logic.
        let avcth = cmd.avctp_header();
        let avch = cmd.avc_header();

        match (avcth.is_single(), avch.subunit_type(), avch.op_code()) {
            // The only type of subunit we support other than panel is unit subunit when a
            // unit info or sub unit info command is sent.
            (true, Some(SubunitType::Unit), &OpCode::UnitInfo) => {
                fx_vlog!(tag: "avctp", 2, "received UNITINFO command");
                // The packet needs to be 8 bytes long according to spec. First three bytes are
                // handled in the response header. Remaining buf is initialized to 0xff.
                let mut pbuf: [u8; 5] = [0xff; 5];
                // This constant is unexplained in the AVC spec but must always be 7.
                pbuf[0] = 0x07;
                // Set unit_type (bits 7-3) set to panel (0x09), and unit (bits 2-0) to 0.
                pbuf[1] = u8::from(&SubunitType::Panel) << 3;
                // Explicitly set company_id to 0xfffff for a generic company.
                pbuf[2] = 0xff;
                pbuf[3] = 0xff;
                pbuf[4] = 0xff;
                match cmd.send_response(ResponseType::ImplementedStable, &pbuf) {
                    Err(e) => Some(Err(e)),
                    Ok(_) => None,
                }
            }
            (true, Some(SubunitType::Unit), &OpCode::SubUnitInfo) => {
                fx_vlog!(tag: "avctp", 2, "received SUBUNITINFO command");
                // The packet needs to be 8 bytes long according to spec. First three bytes are
                // handled in the response header. Remaining buf is initialized to 0xff.
                let mut pbuf: [u8; 5] = [0xff; 5];
                // Set page (bits 6-4) to 0, and set all extension_code (bits 2-0) on.
                pbuf[0] = 0b111;
                // Set subunit_type (bits 7-3) to panel (0x09), and max_subunit_ID (bits 2-0) to 0.
                pbuf[1] = u8::from(&SubunitType::Panel) << 3;
                match cmd.send_response(ResponseType::ImplementedStable, &pbuf) {
                    Err(e) => Some(Err(e)),
                    Ok(_) => None,
                }
            }
            (_, Some(SubunitType::Panel), &OpCode::Passthrough)
            | (_, Some(SubunitType::Panel), &OpCode::VendorDependent) => Some(Ok(cmd)),
            _ => {
                fx_log_info!(tag: "avctp", "received invalid command");
                match cmd.send_response(ResponseType::NotImplemented, &[]) {
                    Err(e) => Some(Err(e)),
                    Ok(_) => None,
                }
            }
        }
    }

    /// Takes the command stream for incoming commands from the remote peer.
    pub fn take_command_stream(&self) -> CommandStream {
        self.inner
            .take_command_stream()
            .filter_map(|avct_command| future::ready(Self::filter_internal_responses(avct_command)))
    }

    /// The maximum amount of time we will wait for a response to a command packet.
    fn passthrough_command_timeout() -> Duration {
        const CMD_TIMER_MS: i64 = 1000;
        Duration::from_millis(CMD_TIMER_MS)
    }

    /// Sends a vendor specific command to the remote peer. Returns a CommandResponseStream to poll
    /// for the responses to the sent command. Returns error if the underlying socket is closed.
    pub fn send_vendor_dependent_command<'a>(
        &'a self,
        command_type: CommandType,
        payload: &'a [u8],
    ) -> Result<impl Stream<Item = Result<CommandResponse>>> {
        let avc_header = Header::new(
            command_type,
            u8::from(&SubunitType::Panel),
            0,
            OpCode::VendorDependent,
            Some(BT_SIG_COMPANY_ID),
        );

        let avc_h_len = avc_header.encoded_len();
        let mut buf = vec![0; avc_h_len];
        avc_header.encode(&mut buf[..])?;
        buf.extend_from_slice(payload);

        let stream = self.inner.send_command(buf.as_slice())?;
        let stream = stream.map(|resp| CommandResponse::try_from(resp?));
        Ok(stream)
    }

    /// Sends an AVC passthrough command to the remote peer. Returns the command response ignoring
    /// any interim responses. Returns error if the underlying socket is closed or the command isn't
    /// acknowledged with an interim response after 1000 ms.
    pub async fn send_avc_passthrough_command<'a>(
        &'a self,
        payload: &'a [u8],
    ) -> Result<CommandResponse> {
        let avc_header = Header::new(
            CommandType::Control,
            u8::from(&SubunitType::Panel),
            0,
            OpCode::Passthrough,
            Some(BT_SIG_COMPANY_ID),
        );

        let avc_h_len = avc_header.encoded_len();
        let mut buf = vec![0; avc_h_len];
        avc_header.encode(&mut buf[..])?;
        buf.extend_from_slice(payload);

        let mut response_stream = self.inner.send_command(buf.as_slice())?;

        let timeout = Time::after(Peer::passthrough_command_timeout());
        loop {
            if let Some(resp) = response_stream
                .next()
                .on_timeout(timeout, || return Some(Err(Error::Timeout)))
                .await
            {
                let value = CommandResponse::try_from(resp?)?;
                if value.0 == ResponseType::Interim {
                    continue;
                }
                return Ok(value);
            } else {
                return Err(Error::PeerDisconnected);
            }
        }
    }
}
