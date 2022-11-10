// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::{self as avctp, AvctpCommand},
    futures::{self, Future},
    packet_encoding::{Decodable, Encodable},
    std::{convert::TryFrom, sync::Arc},
    tracing::{info, trace, warn},
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
    pub fn decode_command(command: &AvctpCommand) -> Result<(PduId, Vec<u8>), Error> {
        let packet_body = command.body();
        if !command.header().is_type(&avctp::AvctpMessageType::Command) {
            // Invalid header type. Send back general reject.
            warn!("Received invalid AVCTP request{:?}", command.header());
            return Err(Error::InvalidMessage);
        }

        let packet = match BrowsePreamble::decode(packet_body) {
            Err(e) => {
                // There was an issue parsing the AVCTP Preamble.
                warn!("Invalid AVCTP Preamble: {:?}", e);
                return Err(Error::InvalidMessage);
            }
            Ok(p) => p,
        };

        let pdu_id = match PduId::try_from(packet.pdu_id) {
            Err(e) => {
                warn!("Received unsupported PduId {:?}: {:?}", packet.pdu_id, e);
                // There was an issue parsing the packet PDU ID.
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
                    .map_err(StatusCode::from)?;

                // Use an arbitrary uid_counter for creating the response. Don't support
                // multiple players, so this value is irrelevant.
                let uid_counter: u16 = 0x1234;
                let media_player_items = folder_items.into_iter().map(Into::into).collect();
                let resp = GetFolderItemsResponse::new_success(uid_counter, media_player_items);

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
                info!("[Browse Channel] Command unimplemented: {:?}", pdu_id);
                // Per AVRCP 1.6, Section 6.15.3, the TG will send `InvalidParameter` for a PDU ID
                // that it did not understand.
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
                    warn!("[Browse Channel] Received invalid command: {:?}", e);
                    return send_general_reject(command, StatusCode::InvalidCommand);
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
                    warn!("[Browse Channel] Couldn't handle command: {:?}", e);
                    return send_general_reject(command, e);
                }
            };

            // Send the response back to the remote peer.
            let response_packet = BrowsePreamble::new(u8::from(&pdu_id), payload);
            let mut response_buf = vec![0; response_packet.encoded_len()];
            response_packet.encode(&mut response_buf[..]).expect("Encoding should work");

            command.send_response(&response_buf[..]).map_err(Into::into)
        }
    }

    /// Clears any state.
    ///
    /// For now, there is no state to be cleared.
    pub fn reset(&self) {}
}

/// Attempts to send a GeneralReject response with the provided `status_code`.
fn send_general_reject(command: AvctpCommand, status_code: StatusCode) -> Result<(), PeerError> {
    let reject_response = BrowsePreamble::general_reject(status_code);
    let mut buf = vec![0; reject_response.encoded_len()];
    reject_response.encode(&mut buf[..]).expect("unable to encode reject packet");
    command.send_response(&buf[..]).map_err(Into::into)
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fuchsia_bluetooth::types::Channel;
    use futures::StreamExt;

    #[fuchsia::test]
    /// Tests handling an invalid browse channel PDU returns an error.
    async fn test_handle_browse_command_invalid_pdu() {
        let delegate = Arc::new(TargetDelegate::new());

        let unsupported_pdu = PduId::GetPlayStatus; // Not a browse channel PDU.
        let args = vec![0x02, 0x0, 0x0];
        let res =
            BrowseChannelHandler::handle_browse_command(unsupported_pdu, args, delegate).await;

        assert_eq!(res, Err(StatusCode::InvalidParameter));
    }

    #[fuchsia::test]
    /// Tests handling  a valid browse channel PDU with no TargetHandler set returns an error.
    async fn test_handle_browse_command_no_handler() {
        let delegate = Arc::new(TargetDelegate::new());

        let supported_pdu = PduId::GetFolderItems;
        let args = vec![0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00];
        let res = BrowseChannelHandler::handle_browse_command(supported_pdu, args, delegate).await;

        assert_eq!(res, Err(StatusCode::NoAvailablePlayers));
    }

    /// Builds and returns the browse channel handler. Also returns a local and remote AVCTP peer
    /// which can be used to send/receive AVCTP commands.
    fn setup_handler_with_remote_peer(
    ) -> (BrowseChannelHandler, avctp::AvctpPeer, avctp::AvctpCommandStream, avctp::AvctpPeer) {
        let handler = BrowseChannelHandler::new(Arc::new(TargetDelegate::new()));

        let (left, right) = Channel::create();
        let local_peer = avctp::AvctpPeer::new(left);
        let local_stream = local_peer.take_command_stream();

        let remote_peer = avctp::AvctpPeer::new(right);

        (handler, local_peer, local_stream, remote_peer)
    }

    #[fuchsia::test]
    async fn handle_invalid_browse_command_sends_reject_but_no_error() {
        let (handler, _local_peer, mut local_stream, remote_peer) =
            setup_handler_with_remote_peer();

        let invalid_command = [0x00, 0x01, 0x02];
        let mut remote_response_stream =
            remote_peer.send_command(&invalid_command).expect("can send command");

        // `bt-avrcp` (e.g local) should receive the command.
        let received_command = local_stream.next().await.unwrap().expect("should receive command");
        // Once handled, we expect the outgoing general reject. Even though it's an invalid command,
        // we don't expect to Error.
        assert_matches!(handler.handle_command(received_command).await, Ok(_));

        // The general reject should be received by the remote.
        let response =
            remote_response_stream.next().await.unwrap().expect("should receive response");
        let payload = BrowsePreamble::decode(response.body()).expect("valid response");
        assert_eq!(payload.pdu_id, u8::from(&PduId::GeneralReject));
    }

    #[fuchsia::test]
    async fn handle_unimplemented_browse_channel_sends_reject_but_no_error() {
        let (handler, _local_peer, mut local_stream, remote_peer) =
            setup_handler_with_remote_peer();

        // Valid PduId
        let unsupported_command = [u8::from(&PduId::PlayItem), 0, 1, 55];
        let mut remote_response_stream =
            remote_peer.send_command(&unsupported_command).expect("can send command");

        // `bt-avrcp` (e.g local) should receive the command.
        let received_command = local_stream.next().await.unwrap().expect("should receive command");
        // Unimplemented command is OK - general reject to let peer know but no Error.
        assert_matches!(handler.handle_command(received_command).await, Ok(_));

        // The general reject should be received by the remote.
        let response =
            remote_response_stream.next().await.unwrap().expect("should receive response");
        let payload = BrowsePreamble::decode(response.body()).expect("valid response");
        assert_eq!(payload.pdu_id, u8::from(&PduId::GeneralReject));
    }

    #[fuchsia::test]
    async fn handle_implemented_valid_browse_channel_command() {
        let (handler, _local_peer, mut local_stream, remote_peer) =
            setup_handler_with_remote_peer();

        // Remote peer sends us a GetTotalNumberOfItems command.
        let cmd = GetTotalNumberOfItemsCommand::new(Scope::MediaPlayerList);
        let mut cmd_buf = vec![0; cmd.encoded_len()];
        let _ = cmd.encode(&mut cmd_buf[..]).expect("valid command");
        let total_command = BrowsePreamble::new(u8::from(&PduId::GetTotalNumberOfItems), cmd_buf);
        let mut total_buf = vec![0; total_command.encoded_len()];
        total_command.encode(&mut total_buf[..]).expect("Encoding should work");
        let mut remote_response_stream =
            remote_peer.send_command(&total_buf).expect("can send command");

        // `bt-avrcp` (e.g local) should receive the command.
        let received_command = local_stream.next().await.unwrap().expect("should receive command");
        // Should handle okay.
        assert_matches!(handler.handle_command(received_command).await, Ok(_));

        // Response should be received by remote.
        let response =
            remote_response_stream.next().await.unwrap().expect("should receive response");
        let payload = BrowsePreamble::decode(response.body()).expect("valid response");
        assert_eq!(payload.pdu_id, u8::from(&PduId::GetTotalNumberOfItems));
    }

    #[fuchsia::test]
    async fn peer_disconnects_while_handling_command_returns_error() {
        let (handler, _local_peer, mut local_stream, remote_peer) =
            setup_handler_with_remote_peer();

        // Remote peer sends us a GetTotalNumberOfItems command.
        let cmd = GetTotalNumberOfItemsCommand::new(Scope::MediaPlayerList);
        let mut cmd_buf = vec![0; cmd.encoded_len()];
        let _ = cmd.encode(&mut cmd_buf[..]).expect("valid command");
        let total_command = BrowsePreamble::new(u8::from(&PduId::GetTotalNumberOfItems), cmd_buf);
        let mut total_buf = vec![0; total_command.encoded_len()];
        total_command.encode(&mut total_buf[..]).expect("Encoding should work");
        let remote_response_stream =
            remote_peer.send_command(&total_buf).expect("can send command");

        // `bt-avrcp` (e.g local) should receive the command.
        let received_command = local_stream.next().await.unwrap().expect("should receive command");

        // Before we get to handle it, the peer disconnects.
        drop(remote_peer);
        drop(remote_response_stream);

        // Handling should return Error.
        assert_matches!(handler.handle_command(received_command).await, Err(_));
    }
}
