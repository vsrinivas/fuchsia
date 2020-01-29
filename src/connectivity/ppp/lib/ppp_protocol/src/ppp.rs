// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides a generic implementation of a PPP control protocol.

use {
    fuchsia_async::futures::future::BoxFuture,
    packet::{Buf, BufferMut, Serializer},
    ppp_packet::{
        CodeRejectPacketBuilder, ConfigurationPacketBuilder, ControlProtocolPacketBuilder,
        PppPacketBuilder, TerminationPacketBuilder,
    },
    std::fmt::Debug,
    thiserror::Error,
};

/// The protocol byte pattern used in IPCP packets.
pub const PROTOCOL_IPV4_CONTROL: u16 = 0x8021;
/// The protocol byte pattern used in IPV6CP packets.
pub const PROTOCOL_IPV6_CONTROL: u16 = 0x8057;
/// The protocol byte pattern used in LCP packets.
pub const PROTOCOL_LINK_CONTROL: u16 = 0xc021;

/// The default LCP MRU, and currently the only supported value by this implementation.
pub const DEFAULT_MRU: u16 = 1500;
/// The default maximum size of a PPP packet, taking into account the default MRU and PPP header
/// bytes.
pub const DEFAULT_MAX_FRAME: u16 = DEFAULT_MRU + 2; // 2 bytes for protocol

/// The code byte pattern used in a Configure-Request packet.
pub const CODE_CONFIGURE_REQUEST: u8 = 1;
/// The code byte pattern used in a Configure-Ack packet.
pub const CODE_CONFIGURE_ACK: u8 = 2;
/// The code byte pattern used in a Configure-Nak packet.
pub const CODE_CONFIGURE_NAK: u8 = 3;
/// The code byte pattern used in a Configure-Rej packet.
pub const CODE_CONFIGURE_REJECT: u8 = 4;
/// The code byte pattern used in a Terminate-Request packet.
pub const CODE_TERMINATE_REQUEST: u8 = 5;
/// The code byte pattern used in a Terminate-Ack packet.
pub const CODE_TERMINATE_ACK: u8 = 6;
/// The code byte pattern used in a Code-Rej packet.
pub const CODE_CODE_REJECT: u8 = 7;

/// Abstraction over the specialized operations of each LCP-like control protocol. The main
/// differences in each protocol deal with parsing and serialization of options.
pub trait ControlProtocol: 'static + Debug + Clone {
    /// A type which can represent any option of the control protocol.
    type Option: Clone + PartialEq + Send + Sync + Debug;
    /// The protocol byte pattern used in this control protocol's packets.
    const PROTOCOL_IDENTIFIER: u16;

    /// Filter out only the unacceptable options from some received options.
    fn unacceptable_options(received: &[Self::Option]) -> Vec<Self::Option>;
    /// Attempt to parse options from a byte buffer.
    fn parse_options(buf: &[u8]) -> Option<Vec<Self::Option>>;
    /// Serialize options into a byte buffer.
    fn serialize_options(options: &[Self::Option]) -> Buf<Vec<u8>>;
}

/// Abstraction over something which can receive PPP packets from something like a driver.
pub trait FrameReceiver: Send + Sync + Debug + 'static {
    /// Asynchronously receive a frame.
    fn rx_frame<'a>(&'a self) -> BoxFuture<'a, Result<Vec<u8>, FrameError>>;
}

/// Abstraction over something which can transmit PPP packets to something like a driver.
pub trait FrameTransmitter: Send + Sync + Debug + 'static {
    /// Asynchronously transmit a frame.
    fn tx_frame<'a>(&'a self, frame: &'a [u8]) -> BoxFuture<'a, Result<(), FrameError>>;
}

/// Possible failures that can result from transitions in the control protocol state machine.
#[derive(Clone, Debug, Error)]
pub enum ProtocolError<P: ControlProtocol> {
    /// Link was terminated.
    #[error("Link was terminated.")]
    Terminated,
    /// Link configuration timed out.
    #[error("Link configuration timed out.")]
    Timeout,
    /// Fatal frame error.
    #[error("Fatal frame error: {:?}.", _0)]
    FatalFrameError(FrameError),
    /// Peer rejected options.
    #[error("Peer rejected options: {:?}.", _0)]
    FatalConfigureRej(Vec<<P as ControlProtocol>::Option>),
    /// Peer rejected code.
    #[error("Peer rejected code in packet: {:x?}.", _0)]
    FatalCodeRej(Vec<u8>),
    /// Peer rejected protocol.
    #[error("Peer rejected protocol: {:x}.", _0)]
    FatalProtocolRej(u16),
    /// Peer sent an invalid configure acknowledge.
    #[error("Peer sent an invalid configure acknowledge.")]
    InvalidAck(ProtocolState<P>),
    /// Peer sent an unacceptable configure request.
    #[error("Peer sent an unacceptable configure request.")]
    UnacceptableReq(ProtocolState<P>),
}

/// Possible failures that can result from sending or receiving frames.
#[derive(Clone, Copy, Debug, Error)]
pub enum FrameError {
    /// Device was down.
    #[error("Device was down.")]
    DeviceDown,
}

impl<P: ControlProtocol> From<FrameError> for ProtocolError<P> {
    fn from(error: FrameError) -> Self {
        ProtocolError::FatalFrameError(error)
    }
}

/// The possible states that the control protocol state machine can be in.
#[derive(Clone, Debug)]
pub enum ProtocolState<P: ControlProtocol> {
    /// No Configure-Request has been sent. This is the initial state.
    Closed(Closed<P>),
    /// A Configure-Request has been sent, but no Configure-Ack has been sent or received.
    RequestSent(RequestSent<P>),
    /// A Configure-Request has been sent, and a Configure-Ack has been sent but not received.
    AckSent(AckSent<P>),
    /// A Configure-Request has been sent, and a Configure-Ack has been received but not sent.
    AckReceived(AckReceived<P>),
    /// A Configure-Request has been sent, and a Configure-Ack has been sent and received.
    Opened(Opened<P>),
}

/// No Configure-Request has been sent. This is the initial state.
#[derive(Clone, Debug)]
pub struct Closed<P: ControlProtocol> {
    desired_options: Vec<<P as ControlProtocol>::Option>,
}

/// A Configure-Request has been sent, but no Configure-Ack has been sent or received.
#[derive(Clone, Debug)]
pub struct RequestSent<P: ControlProtocol> {
    sent_options: Vec<<P as ControlProtocol>::Option>,
    sent_identifier: u8,
    restart_counter: u8,
    restart_time: std::time::Instant,
}

/// A Configure-Request has been sent, and a Configure-Ack has been sent but not received.
#[derive(Clone, Debug)]
pub struct AckReceived<P: ControlProtocol> {
    local_options: Vec<<P as ControlProtocol>::Option>,
    sent_identifier: u8,
}

/// A Configure-Request has been sent, and a Configure-Ack has been received but not sent.
#[derive(Clone, Debug)]
pub struct AckSent<P: ControlProtocol> {
    remote_options: Vec<<P as ControlProtocol>::Option>,
    sent_options: Vec<<P as ControlProtocol>::Option>,
    sent_identifier: u8,
    restart_counter: u8,
    restart_time: std::time::Instant,
}

/// A Configure-Request has been sent, and a Configure-Ack has been sent and received.
#[derive(Clone, Debug)]
pub struct Opened<P: ControlProtocol> {
    local_options: Vec<<P as ControlProtocol>::Option>,
    remote_options: Vec<<P as ControlProtocol>::Option>,
    sent_identifier: u8,
}

impl<P: ControlProtocol> ProtocolState<P> {
    /// Initialize the protocol into a closed state with some desired local options.
    pub fn new(desired_options: Vec<<P as ControlProtocol>::Option>) -> Self {
        ProtocolState::Closed(Closed { desired_options })
    }

    /// If a restart timer is running and the provided time causes the timer to expire, restart the
    /// connection.
    pub async fn restart<T>(
        self,
        transmitter: &T,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        Ok(match self {
            ProtocolState::Closed(closed) => {
                ProtocolState::RequestSent(closed.request(transmitter, time).await?)
            }
            ProtocolState::RequestSent(req_sent) => {
                ProtocolState::RequestSent(req_sent.restart(transmitter, time).await?)
            }
            ProtocolState::AckSent(ack_sent) => {
                ProtocolState::AckSent(ack_sent.restart(transmitter, time).await?)
            }
            _ => self,
        })
    }

    /// Move from the current state into a closed state, preserving local options that may have been
    /// negotiated as the new desired options.
    pub fn reset(self) -> Self {
        ProtocolState::Closed(match self {
            ProtocolState::Closed(closed) => closed,
            ProtocolState::RequestSent(req_sent) => req_sent.reset(),
            ProtocolState::AckSent(ack_sent) => ack_sent.reset(),
            ProtocolState::AckReceived(ack_received) => ack_received.reset(),
            ProtocolState::Opened(opened) => opened.reset(),
        })
    }

    /// Process an incoming Configure-Request packet, send a reply if appropriate, and produce the
    /// new connection state.
    pub async fn rx_configure_req<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        Ok(match self {
            ProtocolState::RequestSent(req_sent) => ProtocolState::AckSent(
                req_sent.rx_configure_req(transmitter, options, identifier).await?,
            ),
            ProtocolState::AckSent(ack_sent) => ProtocolState::AckSent(
                ack_sent.rx_configure_req(transmitter, options, identifier, time).await?,
            ),
            ProtocolState::AckReceived(ack_received) => ProtocolState::Opened(
                ack_received.rx_configure_req(transmitter, options, identifier).await?,
            ),
            ProtocolState::Opened(opened) => ProtocolState::AckSent(
                opened.rx_configure_req(transmitter, options, identifier, time).await?,
            ),
            _ => self,
        })
    }

    /// Process an incoming Configure-Ack packet, send a reply if appropriate, and produce the
    /// new connection state.
    pub async fn rx_configure_ack<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        Ok(match self {
            ProtocolState::RequestSent(req_sent) => ProtocolState::AckReceived(
                req_sent.rx_configure_ack(transmitter, &options, identifier).await?,
            ),
            ProtocolState::AckSent(ack_sent) => ProtocolState::Opened(
                ack_sent.rx_configure_ack(transmitter, &options, identifier).await?,
            ),
            ProtocolState::AckReceived(ack_received) => ProtocolState::AckReceived(
                ack_received.rx_configure_ack(transmitter, &options, identifier, time).await?,
            ),

            ProtocolState::Opened(opened) => ProtocolState::Opened(
                opened.rx_configure_ack(transmitter, &options, identifier, time).await?,
            ),
            _ => self,
        })
    }

    /// Process an incoming Configure-Nak/Configure-Rej packet, send a reply if appropriate, and
    /// produce the new connection state.
    ///
    /// Currently all desired configuration is strict and any nak/reject results in a protocol
    /// error.
    pub async fn rx_configure_rej<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        Ok(match self {
            ProtocolState::RequestSent(req_sent) => ProtocolState::RequestSent(
                req_sent.rx_configure_rej(transmitter, &options, identifier).await?,
            ),
            ProtocolState::AckSent(ack_sent) => ProtocolState::RequestSent(
                ack_sent.rx_configure_rej(transmitter, &options, identifier, time).await?,
            ),
            ProtocolState::AckReceived(ack_received) => ProtocolState::RequestSent(
                ack_received.rx_configure_rej(transmitter, &options, identifier, time).await?,
            ),
            ProtocolState::Opened(opened) => ProtocolState::RequestSent(
                opened.rx_configure_rej(transmitter, &options, identifier, time).await?,
            ),
            _ => self,
        })
    }

    /// Process an incoming Terminate-Request packet, send a reply if appropriate, and produce the
    /// new connection state.
    pub async fn rx_terminate_req<T>(
        self,
        transmitter: &T,
        identifier: u8,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        tx_terminate_ack::<P, _>(transmitter, identifier).await?;
        Err(ProtocolError::Terminated)
    }
}

/// Serialize and transmit a Configure-Request packet with the provided options and identifier.
pub async fn tx_configure_req<'a, P, T>(
    transmitter: &'a T,
    options: &'a [<P as ControlProtocol>::Option],
    identifier: u8,
) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    P: ControlProtocol,
{
    let frame = P::serialize_options(options)
        .encapsulate(ConfigurationPacketBuilder::new())
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_CONFIGURE_REQUEST, identifier))
        .encapsulate(PppPacketBuilder::new(P::PROTOCOL_IDENTIFIER))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    transmitter.tx_frame(frame.as_ref()).await
}

/// Serialize and transmit a Configure-Ack packet with the provided options and identifier.
pub async fn tx_configure_ack<'a, P, T>(
    transmitter: &'a T,
    options: &'a [<P as ControlProtocol>::Option],
    identifier: u8,
) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    P: ControlProtocol,
{
    let frame = P::serialize_options(options)
        .encapsulate(ConfigurationPacketBuilder::new())
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_CONFIGURE_ACK, identifier))
        .encapsulate(PppPacketBuilder::new(P::PROTOCOL_IDENTIFIER))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    transmitter.tx_frame(frame.as_ref()).await
}

/// Serialize and transmit a Configure-Rej packet with the provided options and identifier.
pub async fn tx_configure_rej<'a, P, T>(
    transmitter: &'a T,
    options: &'a [<P as ControlProtocol>::Option],
    identifier: u8,
) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    P: ControlProtocol,
{
    let frame = P::serialize_options(options)
        .encapsulate(ConfigurationPacketBuilder::new())
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_CONFIGURE_REJECT, identifier))
        .encapsulate(PppPacketBuilder::new(P::PROTOCOL_IDENTIFIER))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    transmitter.tx_frame(frame.as_ref()).await
}

/// Serialize and transmit a Terminate-Request packet with the provided identifier.
pub async fn tx_terminate_req<P, T>(transmitter: &T, identifier: u8) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    P: ControlProtocol,
{
    let frame = Buf::new(&mut [], ..)
        .encapsulate(TerminationPacketBuilder::new())
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_TERMINATE_REQUEST, identifier))
        .encapsulate(PppPacketBuilder::new(P::PROTOCOL_IDENTIFIER))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    transmitter.tx_frame(frame.as_ref()).await
}

/// Serialize and transmit a Terminate-Ack packet with the provided identifier.
pub async fn tx_terminate_ack<P, T>(transmitter: &T, identifier: u8) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    P: ControlProtocol,
{
    let frame = Buf::new(&mut [], ..)
        .encapsulate(TerminationPacketBuilder::new())
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_TERMINATE_ACK, identifier))
        .encapsulate(PppPacketBuilder::new(P::PROTOCOL_IDENTIFIER))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    transmitter.tx_frame(frame.as_ref()).await
}

/// Serialize and transmit a Code-Rej packet with the provided identifier.
pub async fn tx_code_rej<P, T, B>(
    transmitter: &T,
    mut buf: B,
    identifier: u8,
) -> Result<(), FrameError>
where
    T: FrameTransmitter,
    P: ControlProtocol,
    B: BufferMut,
{
    buf.shrink_front_to(0);
    let frame = buf
        .encapsulate(CodeRejectPacketBuilder::new())
        .encapsulate(ControlProtocolPacketBuilder::new(CODE_CODE_REJECT, identifier))
        .encapsulate(PppPacketBuilder::new(P::PROTOCOL_IDENTIFIER))
        .serialize_vec_outer()
        .ok()
        .unwrap();
    // truncate the end bytes
    let frame: &[u8] = frame.as_ref();
    let bound = std::cmp::min(frame.len(), DEFAULT_MAX_FRAME as usize);
    transmitter.tx_frame(&frame[..bound]).await
}

const MAX_RESTART: u8 = 10;
const TIMEOUT: std::time::Duration = std::time::Duration::from_secs(3);

impl<P: ControlProtocol> Closed<P> {
    async fn request<T>(
        self,
        transmitter: &T,
        time: std::time::Instant,
    ) -> Result<RequestSent<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        tx_configure_req::<P, _>(transmitter, &self.desired_options, 0).await?;
        Ok(RequestSent {
            sent_options: self.desired_options,
            sent_identifier: 0,
            restart_counter: 1,
            restart_time: time,
        })
    }
}

impl<P: ControlProtocol> RequestSent<P> {
    fn reset(self) -> Closed<P> {
        Closed { desired_options: self.sent_options }
    }

    async fn restart<T>(
        self,
        transmitter: &T,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        if self.restart_counter < MAX_RESTART {
            if time.duration_since(self.restart_time) >= TIMEOUT {
                tx_configure_req::<P, _>(transmitter, &self.sent_options, self.sent_identifier)
                    .await?;
                Ok(Self { restart_counter: self.restart_counter + 1, restart_time: time, ..self })
            } else {
                Ok(self)
            }
        } else {
            Err(ProtocolError::Timeout)
        }
    }

    async fn rx_configure_req<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
    ) -> Result<AckSent<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        let unacceptable_options = P::unacceptable_options(options);
        if unacceptable_options.is_empty() {
            tx_configure_ack::<P, _>(transmitter, options, identifier).await?;
            Ok(AckSent {
                remote_options: options.to_vec(),
                sent_options: self.sent_options,
                sent_identifier: self.sent_identifier,
                restart_counter: self.restart_counter,
                restart_time: self.restart_time,
            })
        } else {
            tx_configure_rej::<P, _>(transmitter, &unacceptable_options, identifier).await?;
            Err(ProtocolError::UnacceptableReq(ProtocolState::RequestSent(self)))
        }
    }

    async fn rx_configure_ack<'a, T>(
        self,
        _transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
    ) -> Result<AckReceived<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        if options == self.sent_options.as_slice() && identifier == self.sent_identifier {
            Ok(AckReceived {
                local_options: self.sent_options,
                sent_identifier: self.sent_identifier,
            })
        } else {
            Err(ProtocolError::InvalidAck(ProtocolState::RequestSent(self)))
        }
    }

    async fn rx_configure_rej<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        _identifier: u8,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        tx_terminate_req::<P, _>(transmitter, self.sent_identifier + 1).await?;
        Err(ProtocolError::FatalConfigureRej(options.to_vec()))
    }
}

impl<P: ControlProtocol> AckReceived<P> {
    fn reset(self) -> Closed<P> {
        Closed { desired_options: self.local_options }
    }

    async fn rx_configure_req<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
    ) -> Result<Opened<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        let unacceptable_options = P::unacceptable_options(options);
        if unacceptable_options.is_empty() {
            tx_configure_ack::<P, _>(transmitter, options, identifier).await?;
            Ok(Opened {
                local_options: self.local_options,
                remote_options: options.to_vec(),
                sent_identifier: self.sent_identifier,
            })
        } else {
            tx_configure_rej::<P, _>(transmitter, &unacceptable_options, identifier).await?;
            Err(ProtocolError::UnacceptableReq(ProtocolState::AckReceived(self)))
        }
    }

    async fn rx_configure_ack<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        if options == self.local_options.as_slice() && identifier == self.sent_identifier {
            Ok(self)
        } else {
            tx_configure_req::<P, _>(transmitter, &self.local_options, self.sent_identifier)
                .await?;
            Err(ProtocolError::InvalidAck(ProtocolState::RequestSent(RequestSent {
                sent_options: self.local_options,
                sent_identifier: self.sent_identifier,
                restart_counter: 1,
                restart_time: time,
            })))
        }
    }

    async fn rx_configure_rej<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        _identifier: u8,
        _time: std::time::Instant,
    ) -> Result<RequestSent<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        tx_terminate_req::<P, _>(transmitter, self.sent_identifier + 1).await?;
        Err(ProtocolError::FatalConfigureRej(options.to_vec()))
    }
}

impl<P: ControlProtocol> AckSent<P> {
    fn reset(self) -> Closed<P> {
        Closed { desired_options: self.sent_options }
    }

    async fn restart<T>(
        self,
        transmitter: &T,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        if self.restart_counter < MAX_RESTART {
            if time.duration_since(self.restart_time) >= TIMEOUT {
                tx_configure_req::<P, _>(transmitter, &self.sent_options, self.sent_identifier)
                    .await?;
                Ok(Self { restart_counter: self.restart_counter + 1, restart_time: time, ..self })
            } else {
                Ok(self)
            }
        } else {
            Err(ProtocolError::Timeout)
        }
    }

    async fn rx_configure_req<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        let unacceptable_options = P::unacceptable_options(options);
        if unacceptable_options.is_empty() {
            tx_configure_ack::<P, _>(transmitter, options, identifier).await?;
            Ok(Self { remote_options: options.to_vec(), ..self })
        } else {
            tx_configure_rej::<P, _>(transmitter, &unacceptable_options, identifier).await?;
            Err(ProtocolError::UnacceptableReq(ProtocolState::RequestSent(RequestSent {
                sent_options: self.sent_options,
                sent_identifier: self.sent_identifier,
                restart_counter: 0,
                restart_time: time,
            })))
        }
    }

    async fn rx_configure_ack<'a, T>(
        self,
        _transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
    ) -> Result<Opened<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        if options == self.sent_options.as_slice() && identifier == self.sent_identifier {
            Ok(Opened {
                local_options: self.sent_options,
                remote_options: self.remote_options,
                sent_identifier: self.sent_identifier,
            })
        } else {
            Err(ProtocolError::InvalidAck(ProtocolState::AckSent(self)))
        }
    }

    async fn rx_configure_rej<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        _identifier: u8,
        _time: std::time::Instant,
    ) -> Result<RequestSent<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        tx_terminate_req::<P, _>(transmitter, self.sent_identifier + 1).await?;
        Err(ProtocolError::FatalConfigureRej(options.to_vec()))
    }
}

impl<P: ControlProtocol> Opened<P> {
    /// Obtain a slice over the local options.
    pub fn local_options(&self) -> &[<P as ControlProtocol>::Option] {
        &self.local_options
    }

    fn reset(self) -> Closed<P> {
        Closed { desired_options: self.local_options }
    }

    async fn rx_configure_req<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<AckSent<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        let unacceptable_options = P::unacceptable_options(options);
        if unacceptable_options.is_empty() {
            tx_configure_req::<P, _>(transmitter, &self.local_options, self.sent_identifier)
                .await?;
            tx_configure_ack::<P, _>(transmitter, options, identifier).await?;
            Ok(AckSent {
                remote_options: options.to_vec(),
                sent_options: self.local_options,
                sent_identifier: self.sent_identifier,
                restart_counter: 1,
                restart_time: time,
            })
        } else {
            tx_configure_rej::<P, _>(transmitter, &unacceptable_options, identifier).await?;
            Err(ProtocolError::UnacceptableReq(ProtocolState::AckReceived(AckReceived {
                local_options: self.local_options,
                sent_identifier: self.sent_identifier,
            })))
        }
    }

    async fn rx_configure_ack<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        identifier: u8,
        time: std::time::Instant,
    ) -> Result<Self, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        if options == self.local_options.as_slice() && identifier == self.sent_identifier {
            Ok(self)
        } else {
            tx_configure_req::<P, _>(transmitter, &self.local_options, self.sent_identifier)
                .await?;
            Err(ProtocolError::InvalidAck(ProtocolState::RequestSent(RequestSent {
                sent_options: self.local_options,
                sent_identifier: self.sent_identifier,
                restart_counter: 1,
                restart_time: time,
            })))
        }
    }

    async fn rx_configure_rej<'a, T>(
        self,
        transmitter: &'a T,
        options: &'a [<P as ControlProtocol>::Option],
        _identifier: u8,
        _time: std::time::Instant,
    ) -> Result<RequestSent<P>, ProtocolError<P>>
    where
        T: FrameTransmitter,
    {
        tx_terminate_req::<P, _>(transmitter, self.sent_identifier + 1).await?;
        Err(ProtocolError::FatalConfigureRej(options.to_vec()))
    }
}
