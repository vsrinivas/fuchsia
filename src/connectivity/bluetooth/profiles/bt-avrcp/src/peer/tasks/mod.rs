// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_bluetooth_bredr::{ConnectParameters, L2capParameters},
    fuchsia_async::DurationExt,
    fuchsia_zircon as zx,
    log::{error, info, trace},
    notification_stream::NotificationStream,
    rand::Rng,
    std::convert::TryInto,
};

mod notification_stream;

use crate::types::PeerError;

/// Processes incoming commands from the control stream and dispatches them to the control command
/// handler. This is started only when we have a connection and when we have either a target or
/// controller SDP profile record for the current peer.
async fn process_control_stream(peer: Arc<RwLock<RemotePeer>>) {
    let (connection, id) = {
        let peer_guard = peer.read();

        match peer_guard.control_channel.connection() {
            Some(connection) => (connection.clone(), peer_guard.id()),
            None => return,
        }
    };

    let command_stream = connection.take_command_stream();

    // Limit to 16 since that is the max number of transactions we can process at any one time per
    // AVCTP
    match command_stream
        .map(Ok)
        .try_for_each_concurrent(16, |command| async {
            let fut = peer.read().control_command_handler.handle_command(command.unwrap());
            let result: Result<(), PeerError> = fut.await;
            result
        })
        .await
    {
        Ok(_) => info!("Peer {} command stream closed", id),
        Err(e) => error!("Peer {} command returned error {:?}", id, e),
    }

    // Command stream closed/errored. Disconnect the peer.
    {
        peer.write().reset_connection(false);
    }
}

/// Processes incoming commands from the browse stream and dispatches them to a
/// browse channel handler.
async fn process_browse_stream(peer: Arc<RwLock<RemotePeer>>) {
    let connection = {
        let peer_guard = peer.read();

        match peer_guard.browse_channel.connection() {
            Some(connection) => connection.clone(),
            None => return,
        }
    };

    let browse_command_stream = connection.take_command_stream();

    // Limit to 16 since that is the max number of transactions we can process at any one time per
    // AVCTP.
    match browse_command_stream
        .map(Ok)
        .try_for_each_concurrent(16, |command| async {
            let fut = peer.read().browse_command_handler.handle_command(command.unwrap());
            let result: Result<(), PeerError> = fut.await;
            result
        })
        .await
    {
        Ok(_) => info!("Peer command stream closed"),
        Err(e) => error!("Peer command returned error {:?}", e),
    }

    // Browse channel closed or errored. Do nothing because the control channel can still exist.
}

/// Handles received notifications from the peer from the subscribed notifications streams and
/// dispatches the notifications back to the controller listeners
fn handle_notification(
    notif: &NotificationEventId,
    peer: &Arc<RwLock<RemotePeer>>,
    data: &[u8],
) -> Result<bool, Error> {
    trace!("received notification for {:?} {:?}", notif, data);

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
fn start_make_connection_task(peer: Arc<RwLock<RemotePeer>>) {
    fasync::Task::spawn(async move {
        let random_delay: zx::Duration =
            zx::Duration::from_nanos(rand::thread_rng().gen_range(
                MIN_CONNECTION_EST_TIME.into_nanos(),
                MAX_CONNECTION_EST_TIME.into_nanos(),
            ));
        trace!(
            "AVRCP waiting {:?} millis before establishing connection",
            random_delay.into_millis()
        );
        fuchsia_async::Timer::new(random_delay.after_now()).await;
        let (peer_id, profile_service) = {
            let peer_guard = peer.read();
            // Return early if we are not in the `Connecting` state.
            match peer_guard.control_channel {
                PeerChannel::Connecting => {}
                _ => return,
            }
            (peer_guard.peer_id, peer_guard.profile_proxy.clone())
        };

        match profile_service
            .connect(
                &mut peer_id.into(),
                &mut ConnectParameters::L2cap(L2capParameters {
                    psm: Some(PSM_AVCTP),
                    ..L2capParameters::new_empty()
                }),
            )
            .await
        {
            Err(fidl_err) => {
                let mut peer_guard = peer.write();
                error!("Profile service connect error: {:?}", fidl_err);
                peer_guard.reset_connection(false);
            }
            Ok(Ok(channel)) => {
                let mut peer_guard = peer.write();
                let channel = match channel.try_into() {
                    Ok(chan) => chan,
                    Err(e) => {
                        error!("Unable to make peer {} from socket: {:?}", peer_id, e);
                        peer_guard.reset_connection(false);
                        return;
                    }
                };
                match peer_guard.control_channel {
                    PeerChannel::Connecting => {
                        let peer = AvcPeer::new(channel);
                        trace!("Successfully established outgoing connection for peer {}", peer_id);
                        peer_guard.set_control_connection(peer);
                    }
                    _ => {
                        trace!(
                            "Incoming connection by peer {} established while making outgoing.",
                            peer_id
                        );

                        // An incoming l2cap connection was made while we were making an
                        // outgoing one. Drop both connections, per spec, and attempt
                        // to reconnect.
                        peer_guard.reset_connection(true);
                    }
                };
            }
            Ok(Err(e)) => {
                error!("Couldn't connect to peer {}: {:?}", peer_id, e);
                let mut peer_guard = peer.write();
                if let PeerChannel::Connecting = peer_guard.control_channel {
                    peer_guard.reset_connection(false);
                }
            }
        }
    })
    .detach()
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
        trace!("creating notification stream for {:?}", notif);
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
                            .unwrap_or_else(|e| { error!("Error decoding packet from peer {:?}", e); true} )
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
    trace!("stopping notifications for {:?}", peer.read().peer_id);
}

/// Starts a task to poll notifications on the remote peer. Aborted when the peer connection is
/// reset.
fn start_notifications_processing_task(peer: Arc<RwLock<RemotePeer>>) -> AbortHandle {
    let (handle, registration) = AbortHandle::new_pair();
    fasync::Task::spawn(
        Abortable::new(
            async move {
                pump_notifications(peer).await;
            },
            registration,
        )
        .map(|_| ()),
    )
    .detach();
    handle
}

/// Starts a task to poll control messages from the peer. Aborted when the peer connection is
/// reset. Started when we have a connection to the remote peer and we have any type of valid SDP
/// profile from the peer.
fn start_control_stream_processing_task(peer: Arc<RwLock<RemotePeer>>) -> AbortHandle {
    let (handle, registration) = AbortHandle::new_pair();
    fasync::Task::spawn(
        Abortable::new(
            async move {
                process_control_stream(peer).await;
            },
            registration,
        )
        .map(|_| ()),
    )
    .detach();
    handle
}

/// Starts a task to poll browse messages from the peer.
/// Started when we have a browse connection to the remote peer as well as an already
/// established control connection.
/// Aborted when the peer connection is reset.
fn start_browse_stream_processing_task(peer: Arc<RwLock<RemotePeer>>) -> AbortHandle {
    let (handle, registration) = AbortHandle::new_pair();

    fasync::Task::spawn(
        Abortable::new(
            async move {
                process_browse_stream(peer).await;
            },
            registration,
        )
        .map(|_| ()),
    )
    .detach();
    handle
}

/// State observer task around a remote peer. Takes a change stream from the remote peer that wakes
/// the task whenever some state has changed on the peer. Swaps tasks such as making outgoing
/// connections, processing the incoming control messages, and registering for notifications on the
/// remote peer.
pub(super) async fn state_watcher(peer: Arc<RwLock<RemotePeer>>) {
    trace!("state_watcher starting");
    let mut change_stream = peer.read().state_change_listener.take_change_stream();
    let peer_weak = Arc::downgrade(&peer);
    drop(peer);

    let mut control_channel_abort_handle: Option<AbortHandle> = None;
    let mut browse_channel_abort_handle: Option<AbortHandle> = None;
    let mut notification_poll_abort_handle: Option<AbortHandle> = None;

    while let Some(_) = change_stream.next().await {
        trace!("state_watcher command received");
        if let Some(peer) = peer_weak.upgrade() {
            let mut peer_guard = peer.write();

            // The old tasks need to be cleaned up. Potentially terminate the control
            // channel processing task and the notification processing task.
            // Reset the `cancel_tasks` flag so that we don't fall into a loop of
            // constantly clearing the tasks.
            if peer_guard.cancel_tasks {
                control_channel_abort_handle.take().map(|a| {
                    trace!("state_watcher: clearing previous control channel task.");
                    a.abort()
                });
                notification_poll_abort_handle.take().map(|a| a.abort());
                peer_guard.cancel_tasks = false;
            }

            trace!("state_watcher control channel {:?}", peer_guard.control_channel);
            match peer_guard.control_channel {
                PeerChannel::Connecting => {}
                PeerChannel::Disconnected => {
                    // Have we discovered service profile data on the peer?
                    if (peer_guard.target_descriptor.is_some()
                        || peer_guard.controller_descriptor.is_some())
                        && peer_guard.attempt_connection
                    {
                        trace!("Starting make_connection task for peer {:?}", peer_guard.peer_id);
                        peer_guard.attempt_connection = false;
                        peer_guard.control_channel = PeerChannel::Connecting;
                        start_make_connection_task(peer.clone());
                    }
                }
                PeerChannel::Connected(_) => {
                    // If we have discovered profile data on this peer, start processing requests
                    // over the control stream.
                    if (peer_guard.target_descriptor.is_some()
                        || peer_guard.controller_descriptor.is_some())
                        && control_channel_abort_handle.is_none()
                    {
                        trace!(
                            "state_watcher: Starting control stream task for peer {}",
                            peer_guard.id()
                        );
                        control_channel_abort_handle =
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

            match peer_guard.browse_channel {
                PeerChannel::Connecting => {}
                PeerChannel::Disconnected => {
                    if let Some(ref abort_handle) = browse_channel_abort_handle {
                        abort_handle.abort();
                        browse_channel_abort_handle = None;
                    }
                }
                PeerChannel::Connected(_) => {
                    // The Browse channel must be established after the control channel.
                    // Ensure that the control channel exists and the browse channel doesn't
                    // before spawning the browse processing task.
                    if control_channel_abort_handle.is_some()
                        && browse_channel_abort_handle.is_none()
                    {
                        browse_channel_abort_handle =
                            Some(start_browse_stream_processing_task(peer.clone()));
                    }
                }
            }
        } else {
            break;
        }
    }

    trace!("state_watcher shutting down. aborting processors");

    // Stop processing state changes on the browse channel.
    // This needs to happen before stopping the control channel.
    if let Some(ref abort_handle) = browse_channel_abort_handle {
        abort_handle.abort();
    }

    // Stop processing state changes on the control channel.
    if let Some(ref abort_handle) = control_channel_abort_handle {
        abort_handle.abort();
    }

    if let Some(ref abort_handle) = notification_poll_abort_handle {
        abort_handle.abort();
    }
}
