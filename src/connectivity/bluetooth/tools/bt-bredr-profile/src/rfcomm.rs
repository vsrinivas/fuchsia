// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    bt_rfcomm::{profile::server_channel_from_protocol, ServerChannel},
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Channel, PeerId, Uuid},
    futures::{channel::mpsc, select, FutureExt, StreamExt, TryStreamExt},
    parking_lot::RwLock,
    std::{convert::TryFrom, sync::Arc},
};

use crate::types::ProfileState;

/// Returns a valid SPP Service Definition.
/// See SPP V12 Table 6.1.
pub fn spp_service_definition() -> bredr::ServiceDefinition {
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(
            bredr::ServiceClassProfileIdentifier::SerialPort as u16,
        )
        .into()]),
        protocol_descriptor_list: Some(vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![],
            },
        ]),
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::SerialPort,
            major_version: 1,
            minor_version: 2,
        }]),
        ..bredr::ServiceDefinition::EMPTY
    }
}

/// Processes data received from the remote peer over the provided RFCOMM `channel`.
/// Processes data in the `write_requests` queue to be sent to the remote peer.
pub async fn rfcomm_channel_task(
    server_channel: ServerChannel,
    state: Arc<RwLock<ProfileState>>,
    mut channel: Channel,
    mut write_requests: mpsc::Receiver<Vec<u8>>,
) {
    loop {
        select! {
            bytes_from_peer = channel.next().fuse() => {
                let user_data = match bytes_from_peer {
                    Some(Ok(bytes)) => bytes,
                    Some(Err(e)) => {
                        println!("Error receiving data: {:?}", e);
                        continue;
                    }
                    None => {
                        // RFCOMM channel closed by the peer.
                        println!("Peer closed RFCOMM channel {:?}", server_channel);
                        state.write().rfcomm.remove_channel(server_channel);
                        return;
                    }
                };
                println!("{:?}: Received user data from peer: {:?}", server_channel, user_data);
            }
            bytes_to_peer = write_requests.next() => {
                match bytes_to_peer {
                    Some(bytes) => {
                        let res = channel.as_ref().write(&bytes);
                        println!("{:?}: Sent user data to peer: {:?}", server_channel, res);
                    }
                    None => return, // RFCOMM channel closed by tool.
                }
            }
            complete => return,
        }
    }
}

/// Processes incoming connection requests over the `connect_requests` stream.
/// Processes incoming search results over the `results_requests` stream.
pub async fn handle_profile_events(
    state: Arc<RwLock<ProfileState>>,
    mut connect_requests: bredr::ConnectionReceiverRequestStream,
    mut results_requests: bredr::SearchResultsRequestStream,
) -> Result<(), Error> {
    loop {
        select! {
            connect_request = connect_requests.try_next() => {
                let bredr::ConnectionReceiverRequest::Connected { protocol, channel, .. } =
                    connect_request?.ok_or(anyhow!("BR/EDR ended service registration"))?;

                // Received an incoming connection request for our advertised service.
                let protocol = protocol.iter().map(Into::into).collect();
                let server_channel =
                    server_channel_from_protocol(&protocol).ok_or(anyhow!("Invalid"))?;
                let channel = Channel::try_from(channel).unwrap();
                // Spawn a processing task to handle read & writes over this RFCOMM channel.
                let receiver = state.write().rfcomm.create_channel(server_channel);
                fasync::Task::spawn(
                    rfcomm_channel_task(server_channel, state.clone(), channel, receiver)
                ).detach();
                println!("Inbound Rfcomm Channel ({:?}) established", server_channel);
            }
            results_request = results_requests.try_next() => {
                let bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, responder, .. } =
                    results_request?.ok_or(anyhow!("BR/EDR ended service search"))?;
                responder.send()?;

                // Discovered an advertised service for the remote peer identified by `peer_id`.
                let id: PeerId = peer_id.into();
                let protocol = protocol.expect("Protocol should exist").iter().map(Into::into).collect();
                let server_channel =
                    server_channel_from_protocol(&protocol)
                    .ok_or(anyhow!("Invalid"))?;
                println!("Found service for {:?} with server channel: {:?}",
                    id.to_string(), server_channel
                );
            }
            complete => return Ok(()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::task::Poll;

    #[test]
    fn rfcomm_task_finishes_when_peer_disconnects() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let server_channel = ServerChannel::try_from(5).expect("valid server channel number");
        let state = Arc::new(RwLock::new(ProfileState::new()));
        let (local, remote) = Channel::create();
        let receiver = state.write().rfcomm.create_channel(server_channel);

        let mut rfcomm_fut =
            Box::pin(rfcomm_channel_task(server_channel, state.clone(), local, receiver));
        assert!(exec.run_until_stalled(&mut rfcomm_fut).is_pending());

        // Sending data to the peer is ok.
        let user_data = vec![0x98, 0x97, 0x96, 0x95];
        assert!(state.write().rfcomm.send_user_data(server_channel, user_data.clone()).is_ok());
        assert!(exec.run_until_stalled(&mut rfcomm_fut).is_pending());

        // "Peer" should receive it.
        {
            let mut vec = Vec::new();
            let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
            match exec.run_until_stalled(&mut remote_fut) {
                Poll::Ready(Ok(received_length)) => assert_eq!(received_length, user_data.len()),
                x => panic!("Expected ready length but got: {:?}", x),
            }
        }

        // Peer sends us data. It should be received gracefully and logged (nothing to test).
        let buf = vec![0x99, 0x11, 0x44];
        let _ = remote.as_ref().write(&buf);
        assert!(exec.run_until_stalled(&mut rfcomm_fut).is_pending());
        // Peer "disconnects" - task is done.
        drop(remote);
        assert!(exec.run_until_stalled(&mut rfcomm_fut).is_ready());
    }

    #[test]
    fn rfcomm_task_finishes_when_tool_closes_channel() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let server_channel = ServerChannel::try_from(5).expect("valid server channel number");
        let state = Arc::new(RwLock::new(ProfileState::new()));
        let (local, _remote) = Channel::create();
        let receiver = state.write().rfcomm.create_channel(server_channel);

        let mut rfcomm_fut =
            Box::pin(rfcomm_channel_task(server_channel, state.clone(), local, receiver));
        assert!(exec.run_until_stalled(&mut rfcomm_fut).is_pending());

        // Tool closes the channel - task is done.
        assert!(state.write().rfcomm.remove_channel(server_channel));
        assert!(exec.run_until_stalled(&mut rfcomm_fut).is_ready());
    }
}
