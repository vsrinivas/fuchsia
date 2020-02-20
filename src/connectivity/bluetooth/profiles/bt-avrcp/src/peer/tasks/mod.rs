// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use super::*;

mod notification_stream;

use notification_stream::NotificationStream;

/// Processes incoming commands from the control stream and dispatches them to the control command
/// handler. This started only when we have connection and when we have either a target or
/// controller SDP profile record for the current peer.
async fn process_control_stream(peer: Arc<RwLock<RemotePeer>>) {
    let connection = {
        let peer_guard = peer.read();

        match peer_guard.control_channel.connection() {
            Some(connection) => connection.clone(),
            None => return,
        }
    };

    let mut command_stream = connection.take_command_stream();

    while let Some(result) = command_stream.next().await {
        match result {
            Ok(command) => {
                let handle_command_fut = { peer.read().command_handler.handle_command(command) };

                if let Err(e) = handle_command_fut.await {
                    fx_log_info!("Command returned error from command handler {:?}", e);
                }
            }
            Err(e) => {
                fx_log_info!("Command stream returned error {:?}", e);
                break;
            }
        }
    }
    // command stream closed or errored. Disconnect the peer.
    {
        peer.write().reset_connection(false);
    }
}

/// Handles received notifications from the peer from the subscribed notifications streams and  
/// dispatches the notifications back to the controller listeners
fn handle_notification(
    notif: &NotificationEventId,
    peer: &Arc<RwLock<RemotePeer>>,
    data: &[u8],
) -> Result<bool, Error> {
    fx_vlog!(tag: "avrcp", 2, "received notification for {:?} {:?}", notif, data);

    let preamble = VendorDependentPreamble::decode(data).map_err(|e| Error::PacketError(e))?;

    let data = &data[preamble.encoded_len()..];

    if data.len() < preamble.parameter_length as usize {
        return Err(Error::UnexpectedResponse);
    }

    match notif {
        NotificationEventId::EventPlaybackStatusChanged => {
            let response = PlaybackStatusChangedNotificationResponse::decode(data)
                .map_err(|e| Error::PacketError(e))?;
            peer.write().handle_new_controller_notification_event(
                ControllerEvent::PlaybackStatusChanged(response.playback_status()),
            );
            Ok(false)
        }
        NotificationEventId::EventTrackChanged => {
            let response = TrackChangedNotificationResponse::decode(data)
                .map_err(|e| Error::PacketError(e))?;
            peer.write().handle_new_controller_notification_event(ControllerEvent::TrackIdChanged(
                response.identifier(),
            ));
            Ok(false)
        }
        NotificationEventId::EventPlaybackPosChanged => {
            let response = PlaybackPosChangedNotificationResponse::decode(data)
                .map_err(|e| Error::PacketError(e))?;
            peer.write().handle_new_controller_notification_event(
                ControllerEvent::PlaybackPosChanged(response.position()),
            );
            Ok(false)
        }
        NotificationEventId::EventVolumeChanged => {
            let response = VolumeChangedNotificationResponse::decode(data)
                .map_err(|e| Error::PacketError(e))?;
            peer.write().handle_new_controller_notification_event(ControllerEvent::VolumeChanged(
                response.volume(),
            ));
            Ok(false)
        }
        _ => Ok(true),
    }
}

/// Starts a task to attempt an outgoing L2CAP connection to remote's AVRCP control channel.
/// The control channel should be in `Connecting` state before spawning this task.
/// TODO(BT-2747): Fix a race where an incoming connection can come in while we are making an
///       outgoing connection. Properly handle the case where we are attempting to connect to remote
///       at the same time they make an incoming connection according to how the the spec says.
fn start_make_connection_task(peer: Arc<RwLock<RemotePeer>>) {
    let peer = peer.clone();

    fasync::spawn(async move {
        let (peer_id, profile_service) = {
            let peer_guard = peer.read();
            // early return if we are not in `Connecting`
            match peer_guard.control_channel {
                PeerChannel::Connecting => {}
                _ => return,
            }
            (peer_guard.peer_id.clone(), peer_guard.profile_svc.clone())
        };

        match profile_service.connect_to_device(&peer_id, PSM_AVCTP as u16).await {
            Ok(socket) => {
                let mut peer_guard = peer.write();
                match peer_guard.control_channel {
                    PeerChannel::Connecting => match AvcPeer::new(socket) {
                        Ok(peer) => {
                            peer_guard.set_control_connection(peer);
                        }
                        Err(e) => {
                            peer_guard.reset_connection(false);
                            fx_log_err!("Unable to make peer from socket {}: {:?}", peer_id, e);
                        }
                    },
                    _ => {
                        fx_log_info!(
                            "incoming connection established while making outgoing {:?}",
                            peer_id
                        );

                        // an incoming l2cap connection was made while we were making an
                        // outgoing one. Drop both connections per spec.
                        peer_guard.reset_connection(false);
                    }
                };
            }
            Err(e) => {
                fx_log_err!("connect_to_device error {}: {:?}", peer_id, e);
                let mut peer_guard = peer.write();
                if let PeerChannel::Connecting = peer_guard.control_channel {
                    peer_guard.reset_connection(false);
                }
            }
        }
    })
}

/// Checks for supported notification on the peer and registers for notifications.
/// This is started on a remote peer when we have a connection and target profile descriptor.
async fn pump_notifications(peer: Arc<RwLock<RemotePeer>>) {
    // events we support when speaking to a peer that supports the target profile.
    const SUPPORTED_NOTIFICATIONS: [NotificationEventId; 4] = [
        NotificationEventId::EventPlaybackStatusChanged,
        NotificationEventId::EventTrackChanged,
        NotificationEventId::EventPlaybackPosChanged,
        NotificationEventId::EventVolumeChanged,
    ];

    let supported_notifications: Vec<NotificationEventId> =
        SUPPORTED_NOTIFICATIONS.iter().cloned().collect();

    // look up what notifications we support on this peer first. Consider updating this from
    // time to time.
    let remote_supported_notifications = match get_supported_events_internal(peer.clone()).await {
        Ok(x) => x,
        Err(_) => return,
    };

    let supported_notifications: Vec<NotificationEventId> = remote_supported_notifications
        .into_iter()
        .filter(|k| supported_notifications.contains(k))
        .collect();

    let mut notification_streams = SelectAll::new();

    for notif in supported_notifications {
        fx_vlog!(tag: "avrcp", 2, "creating notification stream for {:#?}", notif);
        let stream =
            NotificationStream::new(peer.clone(), notif, 1).map_ok(move |data| (notif, data));
        notification_streams.push(stream);
    }

    pin_mut!(notification_streams);
    loop {
        if futures::select! {
            event_result = notification_streams.select_next_some() => {
                match event_result {
                    Ok((notif, data)) => {
                        handle_notification(&notif, &peer, &data[..])
                            .unwrap_or_else(|e| { fx_log_err!("Error decoding packet from peer {:?}", e); true} )
                    },
                    Err(Error::CommandNotSupported) => false,
                    Err(_) => true,
                    _=> true,
                }
            }
            complete => { true }
        } {
            break;
        }
    }
    fx_vlog!(tag: "avrcp", 2, "stopping notifications for {:#?}", peer.read().peer_id);
}

/// Starts a task to poll notifications on the remote peer. Aborted when the peer connection is
/// reset.
fn start_notifications_processing_task(peer: Arc<RwLock<RemotePeer>>) -> AbortHandle {
    let (handle, registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                pump_notifications(peer).await;
            },
            registration,
        )
        .map(|_| ()),
    );
    handle
}

/// Starts a task to poll control messages from the peer. Aborted when the peer connection is
/// reset. Started when we have a connection to the remote peer and we have any type of valid SDP
/// profile from the peer.
fn start_control_stream_processing_task(peer: Arc<RwLock<RemotePeer>>) -> AbortHandle {
    let (handle, registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                process_control_stream(peer).await;
            },
            registration,
        )
        .map(|_| ()),
    );
    handle
}

/// State observer task around a remote peer. Takes a change stream from the remote peer that wakes
/// the task whenever some state has changed on the peer. Swaps tasks such as making outgoing
/// connections, processing the incoming control messages, and registering for notifications on the
/// remote peer.
pub(super) async fn state_watcher(peer: Arc<RwLock<RemotePeer>>) {
    fx_vlog!(tag: "avrcp", 2, "state_watcher starting");
    let mut change_stream = peer.read().state_change_listener.take_change_stream();
    let peer_weak = Arc::downgrade(&peer);
    drop(peer);

    let mut channel_processor_abort_handle: Option<AbortHandle> = None;
    let mut notification_poll_abort_handle: Option<AbortHandle> = None;

    while let Some(_) = change_stream.next().await {
        fx_vlog!(tag: "avrcp", 2, "state_watcher command received");
        if let Some(peer) = peer_weak.upgrade() {
            let mut peer_guard = peer.write();

            fx_vlog!(tag: "avrcp", 2, "make_connection control channel {:?}", peer_guard.control_channel);
            match peer_guard.control_channel {
                PeerChannel::Connecting => {}
                PeerChannel::Disconnected => {
                    if let Some(ref abort_handle) = channel_processor_abort_handle {
                        abort_handle.abort();
                        channel_processor_abort_handle = None;
                    }

                    if let Some(ref abort_handle) = notification_poll_abort_handle {
                        abort_handle.abort();
                        notification_poll_abort_handle = None;
                    }

                    // Have we discovered service profile data on the peer?
                    if (peer_guard.target_descriptor.is_some()
                        || peer_guard.controller_descriptor.is_some())
                        && peer_guard.attempt_connection
                    {
                        fx_vlog!(tag: "avrcp", 2, "make_connection {:?}", peer_guard.peer_id);
                        peer_guard.attempt_connection = false;
                        peer_guard.control_channel = PeerChannel::Connecting;
                        start_make_connection_task(peer.clone());
                    }
                }
                PeerChannel::Connected(_) => {
                    // Have we discovered service profile data on the peer?
                    if (peer_guard.target_descriptor.is_some()
                        || peer_guard.controller_descriptor.is_some())
                        && channel_processor_abort_handle.is_none()
                    {
                        channel_processor_abort_handle =
                            Some(start_control_stream_processing_task(peer.clone()));
                    }

                    if peer_guard.target_descriptor.is_some()
                        && notification_poll_abort_handle.is_none()
                    {
                        notification_poll_abort_handle =
                            Some(start_notifications_processing_task(peer.clone()));
                    }
                }
            }
        } else {
            break;
        }
    }

    fx_vlog!(tag: "avrcp", 2, "state_watcher shutting down. aborting processors");

    // Stop processing state changes entirely on the peer.
    if let Some(ref abort_handle) = channel_processor_abort_handle {
        abort_handle.abort();
    }

    if let Some(ref abort_handle) = notification_poll_abort_handle {
        abort_handle.abort();
    }
}
