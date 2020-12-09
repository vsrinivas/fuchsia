// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::{self as avctp, AvctpCommand},
    futures::{self, Future},
    log::{error, trace},
    packet_encoding::{Decodable, Encodable},
    std::{convert::TryFrom, sync::Arc},
};

use crate::packets::*;
use crate::peer_manager::TargetDelegate;
use crate::types::PeerError;

#[derive(Debug)]
pub struct BrowseChannelHandler {
    target_delegate: Arc<TargetDelegate>,
}

impl BrowseChannelHandler {
    pub fn new(target_delegate: Arc<TargetDelegate>) -> Self {
        Self { target_delegate }
    }

    /// Given an AvctpCommand header and body:
    ///
    /// 1) Decodes the packet body to create an `BrowsePreamble`.
    /// 2) Extracts the PduId from the BrowsePreamble.
    ///
    /// Returns the decoded PduId and the packet body.
    fn decode_command(command: &AvctpCommand) -> Result<(PduId, Vec<u8>), Error> {
        let packet_body = command.body();
        if !command.header().is_type(&avctp::AvctpMessageType::Command) {
            // Invalid header type. Send back general reject.
            trace!("Received AVCTP request that is not a command: {:?}", command.header());
            return Err(Error::InvalidMessage);
        }

        let packet = match BrowsePreamble::decode(packet_body) {
            Err(e) => {
                // There was an issue parsing the AVCTP Preamble. Send back a general reject.
                trace!("Invalid AVCTP Preamble: {:?}", e);
                return Err(Error::InvalidMessage);
            }
            Ok(p) => p,
        };

        let pdu_id = match PduId::try_from(packet.pdu_id) {
            Err(e) => {
                trace!("Received unsupported PduId {:?}: {:?}", packet.pdu_id, e);
                // There was an issue parsing the packet PDU ID. Send back a general reject.
                return Err(Error::InvalidParameter);
            }
            Ok(id) => id,
        };

        Ok((pdu_id, packet.body))
    }

    /// Handles the specific browse channel command specified by `pdu_id`.
    ///
    /// Returns an encoded response buffer to be sent.
    /// Upon error, returns a StatusCode indicating point of failure to send over the
    /// general reject.
    async fn handle_browse_command(
        pdu_id: PduId,
        parameters: Vec<u8>,
        target_delegate: Arc<TargetDelegate>,
    ) -> Result<Vec<u8>, StatusCode> {
        match pdu_id {
            PduId::GetFolderItems => {
                let get_folder_items_cmd = GetFolderItemsCommand::decode(&parameters)
                    .map_err(|_| StatusCode::InvalidParameter)?;
                trace!("Received GetFolderItems Command {:?}", get_folder_items_cmd);

                // Currently, for GetFolderItems, we only support MediaPlayerList scope.
                if get_folder_items_cmd.scope() != Scope::MediaPlayerList {
                    return Err(StatusCode::InvalidParameter);
                }

                // Get the media player items from the TargetDelegate.
                let folder_items = target_delegate
                    .send_get_media_player_items_command()
                    .await
                    .map_err(|e| StatusCode::from(e))?;

                // Use an arbitrary uid_counter for creating the response. Don't support
                // multiple players, so this value is irrelevant.
                let uid_counter: u16 = 0x1234;
                let media_player_items = folder_items.into_iter().map(|i| i.into()).collect();
                let resp = GetFolderItemsResponse::new(
                    StatusCode::Success,
                    uid_counter,
                    media_player_items,
                );

                // Encode the result into the output buffer.
                let mut buf = vec![0; resp.encoded_len()];
                resp.encode(&mut buf[..]).map_err(|_| StatusCode::ParameterContentError)?;

                Ok(buf)
            }
            PduId::GetTotalNumberOfItems => {
                let get_total_items_cmd = GetTotalNumberOfItemsCommand::decode(&parameters)
                    .map_err(|_| StatusCode::InvalidParameter)?;
                trace!("Received GetTotalNumberOfItems Command {:?}", get_total_items_cmd);

                // Currently, for GetTotalNumberOfItems, we only support MediaPlayerList scope.
                if get_total_items_cmd.scope() != Scope::MediaPlayerList {
                    return Err(StatusCode::InvalidParameter);
                }

                // Use an arbitrary uid_counter for creating the response. Don't support
                // multiple players, so this value is irrelevant.
                let uid_counter: u16 = 0x1234;
                let resp = GetTotalNumberOfItemsResponse::new(StatusCode::Success, uid_counter, 1);

                // Encode the result into the output buffer.
                let mut buf = vec![0; resp.encoded_len()];
                resp.encode(&mut buf[..]).map_err(|_| StatusCode::ParameterContentError)?;

                Ok(buf)
            }
            _ => {
                trace!("Browse channel Pdu not handled: {:?}", pdu_id);
                return Err(StatusCode::InvalidParameter);
            }
        }
    }

    /// Given an AvctpCommand, handle command:
    ///
    /// 1) Gets the PduId and packet body from `decode_command`.
    /// 2) Fulfills the command associated with `pdu_id`.
    ///
    /// In any error case (Packet can't be parsed, missing parameters, unsupported PDU, ...),
    /// sends a GeneralReject over the browse channel.
    pub fn handle_command(
        &self,
        command: AvctpCommand,
    ) -> impl Future<Output = Result<(), PeerError>> {
        let target_delegate = self.target_delegate.clone();

        async move {
            // Decode the provided `command` into a PduId and command parameters.
            let (pdu_id, parameters) = match BrowseChannelHandler::decode_command(&command) {
                Ok((id, packet)) => (id, packet),
                Err(e) => {
                    send_general_reject(command, StatusCode::InvalidCommand);
                    return Err(PeerError::PacketError(e));
                }
            };

            // Serve the command based on pdu_id. Returns a buffer payload to send.
            // If an error occurs at any stage of handling the command, a general_reject will be sent.
            let payload = match BrowseChannelHandler::handle_browse_command(
                pdu_id,
                parameters,
                target_delegate,
            )
            .await
            {
                Ok(buf) => buf,
                Err(e) => {
                    send_general_reject(command, e);
                    return Err(PeerError::PacketError(Error::InvalidParameter));
                }
            };

            // Send the response back to the remote peer.
            let response_packet = BrowsePreamble::new(u8::from(&pdu_id), payload);
            let mut response_buf = vec![0; response_packet.encoded_len()];
            response_packet.encode(&mut response_buf[..]).expect("Encoding should work");

            if let Err(e) = command.send_response(&response_buf[..]) {
                error!("Error sending response: {:?}", e);
            }

            Ok(())
        }
    }

    /// Clears any state.
    ///
    /// For now, there is no state to be cleared.
    pub fn reset(&self) {}
}

/// Given a `StatusCode` sends a GeneralReject over the browsing channel.
fn send_general_reject(command: AvctpCommand, status_code: StatusCode) {
    let reject_response = BrowsePreamble::general_reject(status_code);
    let mut buf = vec![0; reject_response.encoded_len()];
    reject_response.encode(&mut buf[..]).expect("unable to encode reject packet");
    if let Err(e) = command.send_response(&buf[..]) {
        error!("Error sending general reject: {:?}", e);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    /// Tests handling an invalid browse channel PDU returns an error.
    async fn test_handle_browse_command_invalid_pdu() {
        let delegate = Arc::new(TargetDelegate::new());

        let unsupported_pdu = PduId::GetPlayStatus; // Not a browse channel PDU.
        let args = vec![0x02, 0x0, 0x0];
        let res =
            BrowseChannelHandler::handle_browse_command(unsupported_pdu, args, delegate).await;

        assert_eq!(res.map_err(|e| format!("{:?}", e)), Err("InvalidParameter".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    /// Tests handling  a valid browse channel PDU with no TargetHandler set returns an error.
    async fn test_handle_browse_command_no_handler() {
        let delegate = Arc::new(TargetDelegate::new());

        let supported_pdu = PduId::GetFolderItems;
        let args = vec![0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00];
        let res = BrowseChannelHandler::handle_browse_command(supported_pdu, args, delegate).await;

        assert_eq!(res.map_err(|e| format!("{:?}", e)), Err("NoAvailablePlayers".to_string()));
    }
}
