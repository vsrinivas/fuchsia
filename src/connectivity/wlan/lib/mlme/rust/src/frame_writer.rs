// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

// Common frame writing functions that are agnostic to the operational mode of
// stations (STA).
//
// This module provides frame writing APIs for frames and partial frames (frame
// bodies). This data is produced by stations regardless of whether or not they
// are operating in AP or client modes.

use {
    crate::error::Error,
    wlan_common::{appendable::Appendable, mac},
};

const BLOCK_ACK_BUFFER_SIZE: u16 = 64; // TODO(29887): Determine better value.
                                       // TODO(29325): Implement QoS policy engine. See the following parts of the
                                       //              specification:
                                       //
                                       //              - IEEE Std 802.11-2016, 3.1 (Traffic Identifier)
                                       //              - IEEE Std 802.11-2016, 5.1.1.1 (Data Service - General)
                                       //              - IEEE Std 802.11-2016, 9.4.2.30 (Access Policy)
                                       //              - IEEE Std 802.11-2016, 9.2.4.5.2 (TID Subfield)
                                       //
                                       //              A TID is from [0, 15] and is assigned to an MSDU in the layers
                                       //              above the MAC. [0, 7] identify Traffic Categories (TCs) and
                                       //              [8, 15] identify parameterized TCs.
const BLOCK_ACK_TID: u16 = 0; // TODO(29325): Implement QoS policy engine.

/// Writes the body of the management frame for an `ADDBA` request to the given
/// buffer. The management header should be written to the buffer before using
/// this function.
///
/// Note that the action header is part of the management frame body and is
/// written by this function. The frame format is described by IEEE Std
/// 802.11-2016, 9.6.5.2.
pub(in crate) fn write_addba_req_body<B: Appendable>(
    buf: &mut B,
    dialog_token: u8,
) -> Result<(), Error> {
    buf.append_value(&mac::ActionHdr { action: mac::ActionCategory::BLOCK_ACK })?;
    let action = mac::BlockAckAction::ADDBA_REQUEST;
    let parameters = mac::BlockAckParameters(0)
        .with_amsdu(true)
        .with_policy(mac::BlockAckPolicy::IMMEDIATE)
        .with_tid(BLOCK_ACK_TID)
        .with_buffer_size(BLOCK_ACK_BUFFER_SIZE);
    let timeout = 0; // TODO(29887): No timeout. Determine better value.
    let starting_sequence_control = mac::BlockAckStartingSequenceControl(0)
        .with_fragment_number(0) // IEEE Std 802.11-2016, 9.6.5.2 - Always zero.
        .with_starting_sequence_number(1); // TODO(29887): Determine better value.
    let addba_req_hdr =
        mac::AddbaReqHdr { action, dialog_token, parameters, timeout, starting_sequence_control };
    buf.append_value(&addba_req_hdr)?;
    Ok(())
}

/// Writes the body of the management frame for an `ADDBA` request to the given
/// buffer. The management header should be written to the buffer before using
/// this function.
///
/// Note that the action header is part fo the management frame body and is
/// written by this function. The frame format is described by IEEE Std
/// 802.11-2016, 9.6.5.3.
pub(in crate) fn write_addba_resp_body<B: Appendable>(
    buf: &mut B,
    dialog_token: u8,
) -> Result<(), Error> {
    buf.append_value(&mac::ActionHdr { action: mac::ActionCategory::BLOCK_ACK })?;
    let action = mac::BlockAckAction::ADDBA_RESPONSE;
    let status = mac::StatusCode::SUCCESS;
    let parameters = mac::BlockAckParameters(0)
        .with_amsdu(true)
        .with_policy(mac::BlockAckPolicy::IMMEDIATE)
        .with_tid(BLOCK_ACK_TID)
        .with_buffer_size(BLOCK_ACK_BUFFER_SIZE);
    let timeout = 0; // TODO(29887): No timeout. Determine better value.
    buf.append_value(&mac::AddbaRespHdr { action, dialog_token, status, parameters, timeout })?;
    Ok(())
}
