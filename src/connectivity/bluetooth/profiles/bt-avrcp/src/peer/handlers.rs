// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

/// Handles commands received from the peer, typically when we are acting in target role for A2DP
/// source and absolute volume support for A2DP sink. Maintains state such as continuations and
/// registered notifications by the peer.
/// FIXME: This is a stub that mostly logs incoming commands as we implement out features in TARGET.
#[derive(Debug)]
pub struct ControlChannelHandler {
    /// Handle back to the remote peer. Weak to prevent a reference cycle since the remote peer owns this object.
    pub remote_peer: Weak<RwLock<RemotePeer>>,
}

impl ControlChannelHandler {
    pub fn new(remote_peer: Weak<RwLock<RemotePeer>>) -> Self {
        Self { remote_peer }
    }

    fn handle_passthrough_command(
        &self,
        remote_peer: &Arc<RwLock<RemotePeer>>,
        command: &AvcCommand,
    ) -> Result<AvcResponseType, Error> {
        let body = command.body();
        let command = AvcPanelCommand::from_primitive(body[0]);

        fx_log_info!("Received passthrough command {:x?} {}", command, &remote_peer.read().peer_id);

        match command {
            Some(_) => Ok(AvcResponseType::Accepted),
            None => Ok(AvcResponseType::Rejected),
        }
    }

    fn handle_notify_vendor_command(&self, _inner: &Arc<PeerManagerInner>, command: &AvcCommand) {
        let packet_body = command.body();

        let preamble = match VendorDependentPreamble::decode(packet_body) {
            Err(e) => {
                fx_log_err!("Unable to parse vendor dependent preamble: {:?}", e);
                // TODO: validate error response is correct. Consider dropping the connection?
                let _ = command.send_response(AvcResponseType::NotImplemented, packet_body);
                return;
            }
            Ok(x) => x,
        };

        let body = &packet_body[preamble.encoded_len()..];

        let pdu_id = match PduId::try_from(preamble.pdu_id) {
            Err(e) => {
                fx_log_err!("Unknown pdu {} received {:#?}: {:?}", preamble.pdu_id, body, e);
                let _ = command.send_response(AvcResponseType::NotImplemented, packet_body);
                return;
            }
            Ok(x) => x,
        };

        // The only PDU that you can send a Notify on is RegisterNotification.
        if pdu_id != PduId::RegisterNotification {
            let reject_response = RejectResponse::new(&pdu_id, &StatusCode::InvalidParameter);
            let packet = reject_response.encode_packet().unwrap();
            let _ = command.send_response(AvcResponseType::Rejected, &packet[..]);
            return;
        }

        match RegisterNotificationCommand::decode(&body[..]) {
            Ok(notification_command) => match *notification_command.event_id() {
                _ => {
                    fx_log_info!(
                        "Unhandled register notification {:?}",
                        *notification_command.event_id()
                    );
                    let reject_response = RejectResponse::new(
                        &PduId::RegisterNotification,
                        &StatusCode::InvalidParameter,
                    );
                    let packet =
                        reject_response.encode_packet().expect("unable to encode rejection packet");
                    // TODO: validate this correct error code to respond in this case.
                    let _ = command.send_response(AvcResponseType::Rejected, &packet[..]);
                    return;
                }
            },
            Err(e) => {
                fx_log_err!(
                    "Unable to decode register notification command {} {:#?}: {:?}",
                    preamble.pdu_id,
                    body,
                    e
                );
                let reject_response =
                    RejectResponse::new(&PduId::RegisterNotification, &StatusCode::InvalidCommand);
                let packet =
                    reject_response.encode_packet().expect("unable to encode rejection packet");
                // TODO: validate this correct error code to respond in this case.
                let _ = command.send_response(AvcResponseType::Rejected, &packet[..]);
                return;
            }
        }
    }

    fn handle_status_vendor_command(
        &self,
        pdu_id: PduId,
        body: &[u8],
    ) -> Result<(AvcResponseType, Vec<u8>), Error> {
        match pdu_id {
            PduId::GetCapabilities => match GetCapabilitiesCommand::decode(body) {
                Ok(get_cap_cmd) => {
                    fx_vlog!(tag: "avrcp", 2, "Received GetCapabilities Command {:#?}", get_cap_cmd);

                    match get_cap_cmd.capability_id() {
                        GetCapabilitiesCapabilityId::CompanyId => {
                            let response = GetCapabilitiesResponse::new_btsig_company();
                            let buf =
                                response.encode_packet().map_err(|e| Error::PacketError(e))?;
                            Ok((AvcResponseType::ImplementedStable, buf))
                        }
                        GetCapabilitiesCapabilityId::EventsId => {
                            let response = GetCapabilitiesResponse::new_events(&[]);
                            let buf =
                                response.encode_packet().map_err(|e| Error::PacketError(e))?;
                            Ok((AvcResponseType::ImplementedStable, buf))
                        }
                    }
                }
                _ => {
                    fx_vlog!(tag: "avrcp", 2, "Unable to parse GetCapabilitiesCommand, sending rejection.");
                    let response = RejectResponse::new(&pdu_id, &StatusCode::InvalidParameter);
                    let buf = response.encode_packet().map_err(|e| Error::PacketError(e))?;
                    Ok((AvcResponseType::Rejected, buf))
                }
            },
            PduId::GetElementAttributes => {
                let get_element_attrib_command =
                    GetElementAttributesCommand::decode(body).map_err(|e| Error::PacketError(e))?;

                fx_vlog!(tag: "avrcp", 2, "Received GetElementAttributes Command {:#?}", get_element_attrib_command);
                let response = GetElementAttributesResponse {
                    title: Some("Hello world".to_string()),
                    ..GetElementAttributesResponse::default()
                };
                let buf = response.encode_packet().map_err(|e| Error::PacketError(e))?;
                Ok((AvcResponseType::ImplementedStable, buf))
            }
            _ => {
                fx_vlog!(tag: "avrcp", 2, "Received unhandled vendor command {:?}", pdu_id);
                Err(Error::CommandNotSupported)
            }
        }
    }

    fn handle_vendor_command(
        &self,
        remote_peer: &Arc<RwLock<RemotePeer>>,
        command: &AvcCommand,
        pmi: &Arc<PeerManagerInner>,
    ) -> Result<(), Error> {
        let packet_body = command.body();
        let preamble = match VendorDependentPreamble::decode(packet_body) {
            Err(e) => {
                fx_log_info!(
                    "Unable to parse vendor dependent preamble {}: {:?}",
                    remote_peer.read().peer_id,
                    e
                );
                // TODO: validate this correct error code to respond in this case.
                let _ = command.send_response(AvcResponseType::NotImplemented, &packet_body[..]);
                fx_vlog!(tag: "avrcp", 2, "Sent NotImplemented response to unparsable command");
                return Ok(());
            }
            Ok(x) => x,
        };
        let body = &packet_body[preamble.encoded_len()..];
        let pdu_id = match PduId::try_from(preamble.pdu_id) {
            Err(e) => {
                fx_log_err!(
                    "Unsupported vendor dependent command pdu {} received from peer {} {:#?}: {:?}",
                    preamble.pdu_id,
                    remote_peer.read().peer_id,
                    body,
                    e
                );
                // recoverable error
                let preamble = VendorDependentPreamble::new_single(preamble.pdu_id, 0);
                let prelen = preamble.encoded_len();
                let mut buf = vec![0; prelen];
                preamble.encode(&mut buf[..]).expect("unable to encode preamble");
                // TODO: validate this correct error code to respond in this case.
                let _ = command.send_response(AvcResponseType::NotImplemented, &buf[..]);
                fx_vlog!(tag: "avrcp", 2, "Sent NotImplemented response to unsupported command {:?}", preamble.pdu_id);
                return Ok(());
            }
            Ok(x) => x,
        };
        fx_vlog!(tag: "avrcp", 2, "Received command PDU {:#?}", pdu_id);
        match command.avc_header().packet_type() {
            AvcPacketType::Command(AvcCommandType::Notify) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=notify command {:?}", pdu_id);
                self.handle_notify_vendor_command(pmi, &command);
                Ok(())
            }
            AvcPacketType::Command(AvcCommandType::Status) => {
                fx_vlog!(tag: "avrcp", 2, "Received ctype=status command {:?}", pdu_id);
                // FIXME: handle_status_vendor_command should better match
                //        handle_notify_vendor_command by sending it's own responses instead
                //        of delegating it back up here to send. Originally it was done this
                //        way so that it would be easier to test but behavior difference is
                //        more confusing than the testability convenience
                match self.handle_status_vendor_command(pdu_id, &body[..]) {
                    Ok((response_type, buf)) => {
                        if let Err(e) = command.send_response(response_type, &buf[..]) {
                            fx_log_err!(
                                "Error sending vendor response to peer {}, {:?}",
                                remote_peer.read().peer_id,
                                e
                            );
                            // unrecoverable
                            return Err(Error::from(e));
                        }
                        fx_vlog!(tag: "avrcp", 2, "sent response {:?} to command {:?}", response_type, pdu_id);
                        Ok(())
                    }
                    Err(e) => {
                        fx_log_err!(
                            "Error parsing command packet from peer {}, {:?}",
                            remote_peer.read().peer_id,
                            e
                        );

                        let response_error_code: StatusCode = match e {
                            Error::CommandNotSupported => {
                                let preamble =
                                    VendorDependentPreamble::new_single(preamble.pdu_id, 0);
                                let prelen = preamble.encoded_len();
                                let mut buf = vec![0; prelen];
                                preamble.encode(&mut buf[..]).expect("unable to encode preamble");
                                if let Err(e) =
                                    command.send_response(AvcResponseType::NotImplemented, &buf[..])
                                {
                                    fx_log_err!(
                                        "Error sending not implemented response to peer {}, {:?}",
                                        remote_peer.read().peer_id,
                                        e
                                    );
                                    return Err(Error::from(e));
                                }
                                fx_vlog!(tag: "avrcp", 2, "sent not implemented response to command {:?}", pdu_id);
                                return Ok(());
                            }
                            Error::PacketError(PacketError::OutOfRange) => {
                                StatusCode::InvalidParameter
                            }
                            Error::PacketError(PacketError::InvalidHeader) => {
                                StatusCode::ParameterContentError
                            }
                            Error::PacketError(PacketError::InvalidMessage) => {
                                StatusCode::ParameterContentError
                            }
                            Error::PacketError(PacketError::UnsupportedMessage) => {
                                StatusCode::InternalError
                            }
                            _ => StatusCode::InternalError,
                        };

                        let pdu_id = PduId::try_from(preamble.pdu_id).expect("Handled above.");
                        let reject_response = RejectResponse::new(&pdu_id, &response_error_code);
                        if let Ok(packet) = reject_response.encode_packet() {
                            if let Err(e) =
                                command.send_response(AvcResponseType::Rejected, &packet[..])
                            {
                                fx_log_err!(
                                    "Error sending vendor reject response to peer {}, {:?}",
                                    remote_peer.read().peer_id,
                                    e
                                );
                                return Err(Error::from(e));
                            }
                            fx_vlog!(tag: "avrcp", 2, "sent not reject response {:?} to command {:?}", response_error_code, pdu_id);
                        } else {
                            // TODO(BT-2220): Audit behavior. Consider different options in this
                            //                error case like dropping the L2CAP socket.
                            fx_log_err!("Unable to encoded reject response. dropping.");
                        }

                        Ok(())
                    }
                }
            }
            _ => {
                let preamble = VendorDependentPreamble::new_single(preamble.pdu_id, 0);
                let prelen = preamble.encoded_len();
                let mut buf = vec![0; prelen];
                preamble.encode(&mut buf[..]).expect("unable to encode preamble");
                let _ = command.send_response(AvcResponseType::NotImplemented, &buf[..]);
                fx_vlog!(tag: "avrcp", 2, "unexpected ctype: {:?}. responding with not implemented {:?}", command.avc_header().packet_type(), pdu_id);
                Ok(())
            }
        }
    }

    pub fn handle_command(
        &self,
        command: AvcCommand,
        pmi: Arc<PeerManagerInner>,
    ) -> Result<(), Error> {
        match Weak::upgrade(&self.remote_peer) {
            Some(remote_peer) => {
                fx_vlog!(tag: "avrcp", 2, "received command {:#?}", command);
                match command.avc_header().op_code() {
                    &AvcOpCode::VendorDependent => {
                        self.handle_vendor_command(&remote_peer, &command, &pmi)
                    }
                    &AvcOpCode::Passthrough => {
                        match self.handle_passthrough_command(&remote_peer, &command) {
                            Ok(response_type) => {
                                if let Err(e) = command.send_response(response_type, &[]) {
                                    fx_log_err!(
                                        "Unable to send passthrough response to peer {}, {:?}",
                                        remote_peer.read().peer_id,
                                        e
                                    );
                                    return Err(Error::from(e));
                                }
                                fx_vlog!(tag: "avrcp", 2, "sent response {:?} to passthrough command", response_type);
                                Ok(())
                            }
                            Err(e) => {
                                fx_log_err!(
                                    "Error parsing command packet from peer {}, {:?}",
                                    remote_peer.read().peer_id,
                                    e
                                );

                                // Sending the error response. This is best effort since the peer
                                // has sent us malformed packet, so we are ignoring and purposefully
                                // not handling and error received to us sending a rejection response.
                                // TODO(BT-2220): audit this behavior and consider logging.
                                let _ = command.send_response(AvcResponseType::Rejected, &[]);
                                fx_vlog!(tag: "avrcp", 2, "sent reject response to passthrough command: {:?}", command);
                                Ok(())
                            }
                        }
                    }
                    _ => {
                        fx_vlog!(tag: "avrcp", 2, "unexpected on command packet: {:?}", command.avc_header().op_code());
                        Ok(())
                    }
                }
            }
            None => panic!("Unexpected state. remote peer should not be deallocated"),
        }
    }
}
