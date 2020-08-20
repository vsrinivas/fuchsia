// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! BlockAck API and state.
//!
//! This module provides a BlockAck state machine and a trait implemented by types that interact
//! with BlockAck. To use the state machine, a type implementing `BlockAckTx` must be provided that
//! transmits BlockAck frames emitted by the state machine.
//!
//! See IEEE Std 802.11-2016, 10.24.

use {
    crate::error::Error,
    fuchsia_zircon as zx,
    log::error,
    wlan_common::{
        appendable::Appendable, buffer_reader::BufferReader, buffer_writer::BufferWriter,
        frame_len, mac,
    },
    wlan_frame_writer::write_frame_with_dynamic_buf,
    wlan_statemachine::*,
    zerocopy::{AsBytes, ByteSlice, LayoutVerified},
};

pub const ADDBA_REQ_FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::ActionHdr, mac::AddbaReqHdr);
pub const ADDBA_RESP_FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::ActionHdr, mac::AddbaRespHdr);
pub const DELBA_FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::ActionHdr, mac::DelbaHdr);

// TODO(29887): Determine a better value.
// TODO(29325): Implement QoS policy engine. See the following parts of the specification:
//
//              - IEEE Std 802.11-2016, 3.1 (Traffic Identifier)
//              - IEEE Std 802.11-2016, 5.1.1.1 (Data Service - General)
//              - IEEE Std 802.11-2016, 9.4.2.30 (Access Policy)
//              - IEEE Std 802.11-2016, 9.2.4.5.2 (TID Subfield)
//
//              A TID is from [0, 15] and is assigned to an MSDU in the layers above the MAC. [0,
//              7] identify Traffic Categories (TCs) and [8, 15] identify parameterized TCs.
const BLOCK_ACK_BUFFER_SIZE: u16 = 64;
const BLOCK_ACK_TID: u16 = 0; // TODO(29325): Implement QoS policy engine.

/// BlockAck transmitter.
///
/// Types that implement this trait can transmit a BlockAck frame body. Typically, this involves
/// embedding the frame body within a management frame with the appropriate metadata and differs
/// based on the state and mode of a STA.
///
/// This trait is used to interact with `BlockAckState` and determines the output state during
/// certain transitions. Moreover, this trait provides the necessary side effects of BlockAck state
/// transitions (namely transmitting frames to clients).
pub trait BlockAckTx {
    /// Transmits a BlockAck frame with the given body.
    ///
    /// The `body` parameter does **not** include any management frame components. The frame length
    /// `n` is the length of the entire management frame, including the management header, action
    /// header, and BlockAck frame. This length can be used to allocate a buffer for the complete
    /// management frame.
    ///
    /// # Errors
    ///
    /// An error should be returned if the frame cannot be constructed or transmitted.
    fn send_block_ack_frame(&mut self, n: usize, body: &[u8]) -> Result<(), Error>;
}

/// Closed BlockAck state.
///
/// A BlockAck session is _closed_ when BlockAck is not in use and no peer has requested to
/// establish or close a session. This is the initial state.
#[derive(Debug, Default)]
pub struct Closed;

#[derive(Debug)]
pub struct Establishing {
    /// The dialog token transmitted to the recipient STA.
    pub dialog_token: u8,
}

/// Established BlockAck state.
///
/// A BlockAck session is _established_ when BlockAck has been negotiated between peers by
/// successfully exchanging ADDBA frames.
#[derive(Debug)]
pub struct Established {
    /// Indicates whether the STA is the originator (initiator) or recipient of the BlockAck
    /// session.
    pub is_initiator: bool,
}

// TODO(29887): BlockAck should be closed if the TID expires before BlockAck or QoS frames are
//              received. The state machine must be driven by a timing mechanism to ensure that
//              this happens. See IEEE Std 802.11-2016, 10.24.5.
statemachine!(
    /// BlockAck state machine.
    ///
    /// Models the state of a BlockAck session. A session is principally established or closed, but
    /// also has intermediate states as a session is negotiated. Establishing and closing sessions
    /// is done using an exchange of ADDBA and DELBA frames.
    ///
    /// A STA that initiates a BlockAck session is known as the _originator_ and its peer the
    /// _recipient_. In infrastructure mode, both APs and clients may initiate a session.
    #[derive(Debug)]
    pub enum BlockAckState,
    () => Closed,
    Closed => [Establishing, Established],
    Establishing => [Closed, Established],
    Established => Closed,
);

impl BlockAckState {
    /// Establishes a BlockAck session.
    ///
    /// Depending on its state, this function may send an ADDBA request frame to the remote peer in
    /// order to await an affirmative ADDBA response frame from that peer.
    ///
    /// See IEEE Std 802.11-2016, 10.24.2.
    #[allow(dead_code)] // TODO(29887): Establish BlockAck sessions to increase throughput.
    pub fn establish(self, tx: &mut impl BlockAckTx) -> Self {
        match self {
            BlockAckState::Closed(state) => {
                // TODO(29887): Examine `CapabilityInfo` of the remote peer.
                // TODO(29887): It appears there is no particular rule to choose the value for
                //              `dialog_token`. Persist the dialog token for the BlockAck session
                //              and find a proven way to generate tokens. See IEEE Std 802.11-2016,
                //              9.6.5.2.
                let dialog_token = 1;
                let mut body = [0u8; ADDBA_REQ_FRAME_LEN];
                let mut writer = BufferWriter::new(&mut body[..]);
                match write_addba_req_body(&mut writer, dialog_token).and_then(|_| {
                    tx.send_block_ack_frame(ADDBA_REQ_FRAME_LEN, writer.into_written())
                }) {
                    Ok(_) => state.transition_to(Establishing { dialog_token }).into(),
                    Err(error) => {
                        error!("error sending ADDBA request frame: {}", error);
                        state.into()
                    }
                }
            }
            _ => self,
        }
    }

    /// Closes a BlockAck session.
    ///
    /// This function sends a DELBA frame to the remote peer unless BlockAck is already closed.
    /// Only initiator peers should attempt to explicitly close BlockAck sessions.
    ///
    /// See IEEE Std 802.11-2016, 10.24.5.
    #[allow(dead_code)] // TODO(29887): Implement the datagrams and transmission of DELBA frames.
    pub fn close(self, tx: &mut impl BlockAckTx, reason_code: mac::ReasonCode) -> Self {
        // This aggressively transitions to the `Closed` state. DELBA frames do not require an
        // exchange (as ADDBA frames do). Note that per IEEE Std 802.11-2016, 10.24.5, only the
        // initiator is meant to transmit DELBA frames. The other mechanism for closing BlockAck is
        // an expiration of the TID before any BlockAck or QoS frames are received.
        match self {
            BlockAckState::Closed(_) => self,
            _ => {
                let is_initiator = match &self {
                    &BlockAckState::Establishing(..) => true,
                    &BlockAckState::Established(State {
                        data: Established { is_initiator },
                        ..
                    }) => is_initiator,
                    _ => false,
                };
                let mut body = [0u8; DELBA_FRAME_LEN];
                let mut writer = BufferWriter::new(&mut body[..]);
                match write_delba_body(&mut writer, is_initiator, reason_code)
                    .and_then(|_| tx.send_block_ack_frame(DELBA_FRAME_LEN, writer.into_written()))
                {
                    Ok(_) => BlockAckState::from(State::new(Closed)),
                    Err(error) => {
                        error!("error sending DELBA frame: {}", error);
                        self
                    }
                }
            }
        }
    }

    /// Reacts to a BlockAck frame.
    ///
    /// This function transitions the state machine in response to a BlockAck frame. In particular,
    /// this function reacts to ADDBA and DELBA frames to begin and end BlockAck sessions.
    ///
    /// The `body` parameter must **not** include the management action byte. This value should be
    /// parsed beforehand and removed from the frame body.
    pub fn on_block_ack_frame<B: ByteSlice>(
        self,
        tx: &mut impl BlockAckTx,
        action: mac::BlockAckAction,
        body: B,
    ) -> Self {
        match self {
            BlockAckState::Closed(state) => match action {
                mac::BlockAckAction::ADDBA_REQUEST => {
                    // Read the ADDBA request and send a response. If successful, transition to
                    // `Established`. See IEEE Std 802.11-2016, 10.24.2.
                    let mut frame = [0u8; ADDBA_RESP_FRAME_LEN];
                    let mut writer = BufferWriter::new(&mut frame[..]);
                    match read_addba_req_hdr(body)
                        .and_then(|request| {
                            write_addba_resp_body(&mut writer, request.dialog_token)
                        })
                        .and_then(|_| {
                            tx.send_block_ack_frame(ADDBA_RESP_FRAME_LEN, writer.into_written())
                        }) {
                        Ok(_) => state.transition_to(Established { is_initiator: false }).into(),
                        Err(error) => {
                            error!("error sending ADDBA response frame: {}", error);
                            state.into()
                        }
                    }
                }
                _ => state.into(),
            },
            BlockAckState::Establishing(state) => match action {
                mac::BlockAckAction::ADDBA_RESPONSE => {
                    // Read the ADDBA response. If successful and the response is affirmative,
                    // transition to `Established`. If the response is negative, transition to
                    // `Closed`. See IEEE Std 802.11-2016, 10.24.2.
                    match read_addba_resp_hdr(state.dialog_token, body) {
                        Ok(response) => {
                            if { response.status } == mac::StatusCode::SUCCESS {
                                state.transition_to(Established { is_initiator: true }).into()
                            } else {
                                // Transition to `Closed` if the remote peer sends a negative
                                // response.
                                state.transition_to(Closed).into()
                            }
                        }
                        Err(error) => {
                            error!("error processing ADDBA response frame: {}", error);
                            // Transition to `Closed` if any errors occur.
                            state.transition_to(Closed).into()
                        }
                    }
                }
                mac::BlockAckAction::DELBA => state.transition_to(Closed).into(),
                _ => state.into(),
            },
            BlockAckState::Established(state) => match action {
                mac::BlockAckAction::DELBA => {
                    // TODO(29887): Examine the DELBA frame as needed.  This is necessary for GCR
                    //              modes, for example.
                    if let Err(error) = read_delba_hdr(body) {
                        error!("error processing DELBA frame: {}", error);
                    }
                    // See IEEE Std 802.11-2016, 10.24.5.
                    state.transition_to(Closed).into()
                }
                _ => state.into(),
            },
        }
    }
}

/// Writes the body of the management frame for an `ADDBA` request to the given buffer. The
/// management header should be written to the buffer before using this function.
///
/// Note that the action header is part of the management frame body and is written by this
/// function. The frame format is described by IEEE Std 802.11-2016, 9.6.5.2.
pub fn write_addba_req_body<B: Appendable>(
    buffer: &mut B,
    dialog_token: u8,
) -> Result<usize, Error> {
    let body = mac::AddbaReqHdr {
        action: mac::BlockAckAction::ADDBA_REQUEST,
        dialog_token,
        parameters: mac::BlockAckParameters(0)
            .with_amsdu(true)
            .with_policy(mac::BlockAckPolicy::IMMEDIATE)
            .with_tid(BLOCK_ACK_TID)
            .with_buffer_size(BLOCK_ACK_BUFFER_SIZE),
        timeout: 0, // TODO(29887): No timeout. Determine a better value.
        starting_sequence_control: mac::BlockAckStartingSequenceControl(0)
            .with_fragment_number(0) // Always zero. See IEEE Std 802.11-2016, 9.6.5.2.
            .with_starting_sequence_number(1), // TODO(29887): Determine a better value.
    };
    write_frame_with_dynamic_buf!(
        buffer,
        {
            headers: {
                mac::ActionHdr: &mac::ActionHdr {
                    action: mac::ActionCategory::BLOCK_ACK,
                },
            },
            body: body.as_bytes(),
        }
    )
    .map(|(_, n)| n)
}

/// Writes the body of the management frame for an `ADDBA` request to the given buffer. The
/// management header should be written to the buffer before using this function.
///
/// Note that the action header is part fo the management frame body and is written by this
/// function. The frame format is described by IEEE Std 802.11-2016, 9.6.5.3.
pub fn write_addba_resp_body<B: Appendable>(
    buffer: &mut B,
    dialog_token: u8,
) -> Result<usize, Error> {
    let body = mac::AddbaRespHdr {
        action: mac::BlockAckAction::ADDBA_RESPONSE,
        dialog_token,
        status: mac::StatusCode::SUCCESS,
        parameters: mac::BlockAckParameters(0)
            .with_amsdu(true)
            .with_policy(mac::BlockAckPolicy::IMMEDIATE)
            .with_tid(BLOCK_ACK_TID)
            .with_buffer_size(BLOCK_ACK_BUFFER_SIZE),
        timeout: 0, // TODO(29887): No timeout. Determina a better value.
    };
    write_frame_with_dynamic_buf!(
        buffer,
        {
            headers: {
                mac::ActionHdr: &mac::ActionHdr {
                    action: mac::ActionCategory::BLOCK_ACK,
                },
            },
            body: body.as_bytes(),
        }
    )
    .map(|(_, n)| n)
}

pub fn write_delba_body<B: Appendable>(
    buffer: &mut B,
    is_initiator: bool,
    reason_code: mac::ReasonCode,
) -> Result<usize, Error> {
    let body = mac::DelbaHdr {
        action: mac::BlockAckAction::DELBA,
        parameters: mac::DelbaParameters(0).with_initiator(is_initiator).with_tid(BLOCK_ACK_TID),
        reason_code,
    };
    write_frame_with_dynamic_buf!(
        buffer,
        {
            headers: {
                mac::ActionHdr: &mac::ActionHdr {
                    action: mac::ActionCategory::BLOCK_ACK,
                },
            },
            body: body.as_bytes(),
        }
    )
    .map(|(_, n)| n)
}

/// Reads an ADDBA request header from an ADDBA frame body.
///
/// This function and others in this module do **not** expect the management action byte to be
/// present in the body. This value should be parsed and removed beforehand.
///
/// # Errors
///
/// Returns an error if the header cannot be parsed.
fn read_addba_req_hdr<B: ByteSlice>(body: B) -> Result<LayoutVerified<B, mac::AddbaReqHdr>, Error> {
    let mut reader = BufferReader::new(body);
    reader.read::<mac::AddbaReqHdr>().ok_or_else(|| {
        Error::Status("error reading ADDBA request header".to_string(), zx::Status::IO)
    })
}

/// Reads an ADDBA response header from an ADDBA frame body.
///
/// This function and others in this module do **not** expect the management action byte to be
/// present in the body. This value should be parsed and removed beforehand.
///
/// # Errors
///
/// Returns an error if the header cannot be parsed or if its dialog token is not the same as the
/// given parameters.
fn read_addba_resp_hdr<B: ByteSlice>(
    dialog_token: u8,
    body: B,
) -> Result<LayoutVerified<B, mac::AddbaRespHdr>, Error> {
    let mut reader = BufferReader::new(body);
    reader
        .read::<mac::AddbaRespHdr>()
        .ok_or_else(|| {
            Error::Status("error reading ADDBA response header".to_string(), zx::Status::IO)
        })
        .and_then(|response| {
            if response.dialog_token == dialog_token {
                Ok(response)
            } else {
                Err(Error::Status(
                    "mismatched dialog token in ADDBA response header".to_string(),
                    zx::Status::IO,
                ))
            }
        })
}

/// Reads a DELBA header from a DELBA frame body.
///
/// This function and others in this module do **not** expect the management action byte to be
/// present in the body. This value should be parsed and removed beforehand.
///
/// # Errors
///
/// Returns an error if the header cannot be parsed.
fn read_delba_hdr<B: ByteSlice>(body: B) -> Result<LayoutVerified<B, mac::DelbaHdr>, Error> {
    let mut reader = BufferReader::new(body);
    reader
        .read::<mac::DelbaHdr>()
        .ok_or_else(|| Error::Status("error reading DELBA header".to_string(), zx::Status::IO))
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::error::Error, fuchsia_zircon as zx, wlan_common::assert_variant,
        wlan_statemachine as statemachine,
    };

    /// A STA that can send ADDBA frames (implements the `BlockAckTx` trait).
    enum Station {
        /// When in the this state, all transmissions succeed.
        Up,
        /// When in the this state, all transmissions fail.
        Down,
    }

    impl BlockAckTx for Station {
        fn send_block_ack_frame(&mut self, _: usize, _: &[u8]) -> Result<(), Error> {
            match *self {
                Station::Up => Ok(()),
                Station::Down => {
                    Err(Error::Status(format!("failed to transmit BlockAck frame"), zx::Status::IO))
                }
            }
        }
    }

    /// Creates an ADDBA request body.
    ///
    /// Note that this is not a complete ADDBA request frame. This function exercises
    /// `write_addba_req_body`.
    fn addba_req_body(dialog_token: u8) -> (usize, [u8; ADDBA_RESP_FRAME_LEN]) {
        let mut body = [0u8; ADDBA_RESP_FRAME_LEN];
        let mut writer = BufferWriter::new(&mut body[..]);
        super::write_addba_req_body(&mut writer, dialog_token).unwrap();
        (writer.bytes_written(), body)
    }

    /// Creates an ADDBA response body.
    ///
    /// Note that this is not a complete ADDBA response frame. This function exercises
    /// `write_addba_resp_body`.
    fn addba_resp_body(dialog_token: u8) -> (usize, [u8; ADDBA_RESP_FRAME_LEN]) {
        let mut body = [0u8; ADDBA_RESP_FRAME_LEN];
        let mut writer = BufferWriter::new(&mut body[..]);
        super::write_addba_resp_body(&mut writer, dialog_token).unwrap();
        (writer.bytes_written(), body)
    }

    /// Creates a DELBA body.
    ///
    /// Note that this is not a complete DELBA frame. This function exercises `write_delba_body`.
    fn delba_body(
        is_initiator: bool,
        reason_code: mac::ReasonCode,
    ) -> (usize, [u8; DELBA_FRAME_LEN]) {
        let mut body = [0u8; DELBA_FRAME_LEN];
        let mut writer = BufferWriter::new(&mut body[..]);
        super::write_delba_body(&mut writer, is_initiator, reason_code).unwrap();
        (writer.bytes_written(), body)
    }

    #[test]
    fn request_establish_block_ack() {
        let mut station = Station::Up;
        let state = BlockAckState::from(State::new(Closed));
        let state = state.establish(&mut station);
        assert_variant!(state, BlockAckState::Establishing(_), "not in `Establishing` state");

        let mut station = Station::Down;
        let state = BlockAckState::from(State::new(Closed));
        let state = state.establish(&mut station);
        assert_variant!(state, BlockAckState::Closed(_), "not in `Closed` state");
    }

    #[test]
    fn request_close_block_ack() {
        let mut station = Station::Up;
        let state = BlockAckState::from(statemachine::testing::new_state(Established {
            is_initiator: true,
        }));
        let state = state.close(&mut station, mac::ReasonCode::UNSPECIFIED_REASON);
        assert_variant!(state, BlockAckState::Closed(_), "not in `Closed` state");
    }

    #[test]
    fn respond_establish_block_ack() {
        // Create a buffer describing an ADDBA request body and read the management action byte.
        let (n, body) = addba_req_body(1);
        let body = &body[..n];
        let (_, body) =
            LayoutVerified::<_, mac::ActionHdr>::new_unaligned_from_prefix(body).unwrap();

        let mut station = Station::Up;
        let state = BlockAckState::from(State::new(Closed));
        let state =
            state.on_block_ack_frame(&mut station, mac::BlockAckAction::ADDBA_REQUEST, body);
        assert_variant!(state, BlockAckState::Established(_), "not in `Established` state");

        let mut station = Station::Down;
        let state = BlockAckState::from(State::new(Closed));
        let state =
            state.on_block_ack_frame(&mut station, mac::BlockAckAction::ADDBA_REQUEST, body);
        assert_variant!(state, BlockAckState::Closed(_), "not in `Closed` state");
    }

    #[test]
    fn respond_close_block_ack() {
        // Create a buffer describing a DELBA body and read the management action byte.
        let (n, body) = delba_body(true, mac::ReasonCode::UNSPECIFIED_REASON);
        let body = &body[..n];
        let (_, body) =
            LayoutVerified::<_, mac::ActionHdr>::new_unaligned_from_prefix(body).unwrap();

        let mut station = Station::Up;
        let state = BlockAckState::from(statemachine::testing::new_state(Established {
            is_initiator: false,
        }));
        let state = state.on_block_ack_frame(&mut station, mac::BlockAckAction::DELBA, body);
        assert_variant!(state, BlockAckState::Closed(_), "not in `Closed` state");
    }

    #[test]
    fn write_addba_req_body() {
        let (n, body) = addba_req_body(1);
        let body = &body[..n];
        assert_eq!(
            body,
            &[
                // Action frame header (also part of ADDBA request frame)
                0x03, // Action Category: block ack (0x03)
                0x00, // block ack action: ADDBA request (0x00)
                1,    // block ack dialog token
                0b00000011, 0b00010000, // block ack parameters (u16)
                0, 0, // block ack timeout (u16) (0: disabled)
                0b00010000, 0, // block ack starting sequence number: fragment 0, sequence 1
            ][..]
        );
    }

    #[test]
    fn write_addba_resp_body() {
        let (n, body) = addba_resp_body(1);
        let body = &body[..n];
        assert_eq!(
            body,
            &[
                // Action frame header (also part of ADDBA response frame)
                0x03, // Action Category: block ack (0x03)
                0x01, // block ack action: ADDBA response (0x01)
                1,    // block ack dialog token
                0, 0, // status
                0b00000011, 0b00010000, // block ack parameters (u16)
                0, 0, // block ack timeout (u16) (0: disabled)
            ][..]
        );
    }

    #[test]
    fn write_delba_body() {
        let (n, body) = delba_body(true, mac::ReasonCode::UNSPECIFIED_REASON);
        let body = &body[..n];
        assert_eq!(
            body,
            &[
                // Action frame header (also part of DELBA frame)
                0x03, // action category: block ack (0x03)
                0x02, // block ack action: DELBA (0x02)
                0b00000000, 0b00001000, // DELBA block ack parameters (u16)
                1, 0, // reason code (u16) (1: unspecified reason)
            ][..]
        );
    }
}
