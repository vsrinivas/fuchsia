// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::AvcPeer,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_async::DurationExt,
    fuchsia_zircon as zx,
    futures::{
        future::FutureExt,
        stream::{SelectAll, StreamExt, TryStreamExt},
    },
    notification_stream::NotificationStream,
    packet_encoding::{Decodable, Encodable},
    parking_lot::RwLock,
    rand::Rng,
    std::{convert::TryInto, sync::Arc},
    tracing::{error, info, trace},
};

mod notification_stream;

use crate::peer::*;
use crate::types::PeerError as Error;

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
            let result: Result<(), Error> = fut.await;
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
            let result: Result<(), Error> = fut.await;
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

/// Attempt an outgoing L2CAP connection to remote's AVRCP control channel.
/// The control channel should be in `Connecting` state before spawning this task.
async fn make_connection(peer: Arc<RwLock<RemotePeer>>) {
    let random_delay: zx::Duration = zx::Duration::from_nanos(
        rand::thread_rng()
            .gen_range(MIN_CONNECTION_EST_TIME.into_nanos(), MAX_CONNECTION_EST_TIME.into_nanos()),
    );
    trace!("AVRCP waiting {:?} millis before establishing connection", random_delay.into_millis());
    fuchsia_async::Timer::new(random_delay.after_now()).await;
    let (peer_id, profile_service) = {
        let peer_guard = peer.read();
        // Return early if we are not in the `Connecting` state.
        if !peer_guard.control_channel.is_connecting() {
            return;
        }
        (peer_guard.peer_id, peer_guard.profile_proxy.clone())
    };

    match profile_service
        .connect(
            &mut peer_id.into(),
            &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters {
                psm: Some(bredr::PSM_AVCTP),
                ..bredr::L2capParameters::EMPTY
            }),
        )
        .await
    {
        Err(fidl_err) => {
            let mut peer_guard = peer.write();
            warn!("Profile service connect error: {:?}", fidl_err);
            peer_guard.inspect().metrics().connection_error();
            peer_guard.reset_connection(false);
        }
        Ok(Ok(channel)) => {
            let mut peer_guard = peer.write();
            let channel = match channel.try_into() {
                Ok(chan) => chan,
                Err(e) => {
                    error!("Unable to make peer {} from socket: {:?}", peer_id, e);
                    peer_guard.inspect().metrics().connection_error();
                    peer_guard.reset_connection(false);
                    return;
                }
            };
            if peer_guard.control_channel.is_connecting() {
                let peer = AvcPeer::new(channel);
                trace!("Successfully established outgoing connection for peer {}", peer_id);
                peer_guard.set_control_connection(peer);
            } else {
                trace!("Connection collision from peer {} while making outgoing.", peer_id);

                // An incoming l2cap connection was made while we were making an
                // outgoing one. Drop both connections, per spec, and attempt
                // to reconnect.
                peer_guard.reset_connection(true);
            }
        }
        Ok(Err(e)) => {
            error!("Couldn't connect to peer {}: {:?}", peer_id, e);
            let mut peer_guard = peer.write();
            peer_guard.inspect().metrics().connection_error();
            if peer_guard.control_channel.is_connecting() {
                peer_guard.reset_connection(false);
            }
        }
    }
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

    let local_supported: HashSet<NotificationEventId> =
        SUPPORTED_NOTIFICATIONS.iter().cloned().collect();

    // look up what notifications we support on this peer first. Consider updating this from
    // time to time.
    let remote_supported = match get_supported_events_internal(peer.clone()).await {
        Ok(x) => x,
        Err(_) => return,
    };

    let supported_notifications = remote_supported.intersection(&local_supported);

    let mut notification_streams = SelectAll::new();

    for notif in supported_notifications {
        trace!("creating notification stream for {:?}", notif);
        let stream = NotificationStream::new(peer.clone(), notif.clone(), 1)
            .map_ok(move |data| (notif, data));
        notification_streams.push(stream);
    }

    while let Some(event_result) = notification_streams.next().await {
        match event_result.map(|(notif, data)| handle_notification(&notif, &peer, &data[..])) {
            Err(Error::CommandNotSupported) => continue,
            Err(_) => break,
            Ok(Err(e)) => {
                warn!("Error decoding packet from peer {:?}", e);
                break;
            }
            Ok(Ok(true)) => break,
            Ok(Ok(false)) => continue,
        }
    }
    trace!("stopping notifications for {:?}", peer.read().peer_id);
}

/// Starts a task to poll notifications on the remote peer. Aborted when the peer connection is
/// reset.
fn start_notifications_processing_task(peer: Arc<RwLock<RemotePeer>>) -> fasync::Task<()> {
    fasync::Task::spawn(pump_notifications(peer).map(|_| ()))
}

/// Starts a task to poll control messages from the peer. Aborted when the peer connection is
/// reset. Started when we have a connection to the remote peer and we have any type of valid SDP
/// profile from the peer.
fn start_control_stream_processing_task(peer: Arc<RwLock<RemotePeer>>) -> fasync::Task<()> {
    fasync::Task::spawn(process_control_stream(peer).map(|_| ()))
}

/// Starts a task to poll browse messages from the peer.
/// Started when we have a browse connection to the remote peer as well as an already
/// established control connection.
/// Aborted when the peer connection is reset.
fn start_browse_stream_processing_task(peer: Arc<RwLock<RemotePeer>>) -> fasync::Task<()> {
    fasync::Task::spawn(process_browse_stream(peer).map(|_| ()))
}

/// State observer task around a remote peer. Takes a change stream from the remote peer that wakes
/// the task whenever some state has changed on the peer. Swaps tasks such as making outgoing
/// connections, processing the incoming control messages, and registering for notifications on the
/// remote peer.
pub(super) async fn state_watcher(peer: Arc<RwLock<RemotePeer>>) {
    trace!("state_watcher starting");
    let mut change_stream = peer.read().state_change_listener.take_change_stream();
    let id = peer.read().id();
    let peer_weak = Arc::downgrade(&peer);
    drop(peer);

    let mut make_connection_task = fasync::Task::spawn(futures::future::ready(()));
    let mut control_channel_task: Option<fasync::Task<()>> = None;
    let mut browse_channel_task: Option<fasync::Task<()>> = None;
    let mut notification_poll_task: Option<fasync::Task<()>> = None;

    while let Some(_) = change_stream.next().await {
        trace!("state_watcher command received");
        let peer = match peer_weak.upgrade() {
            Some(peer) => peer,
            None => break,
        };
        let mut peer_guard = peer.write();

        // The old tasks need to be cleaned up. Potentially terminate the channel
        // processing tasks and the notification processing task.
        // Reset the `cancel_tasks` flag so that we don't fall into a loop of
        // constantly clearing the tasks.
        if peer_guard.cancel_tasks {
            if browse_channel_task.take().is_some() {
                trace!("state_watcher: clearing previous browse channel task.");
            }
            if control_channel_task.take().is_some() {
                trace!("state_watcher: clearing previous control channel task.");
            }
            notification_poll_task = None;
            peer_guard.cancel_tasks = false;
        }

        trace!("state_watcher control channel {:?}", peer_guard.control_channel);
        match peer_guard.control_channel.state() {
            &PeerChannelState::Connecting => {}
            &PeerChannelState::Disconnected => {
                // Have we discovered service profile data on the peer?
                if peer_guard.discovered() && peer_guard.attempt_connection {
                    trace!("Starting make_connection task for peer {}", id);
                    peer_guard.attempt_connection = false;
                    peer_guard.control_channel.connecting();
                    make_connection_task = fasync::Task::spawn(make_connection(peer.clone()));
                }
            }
            &PeerChannelState::Connected(_) => {
                // If we have discovered profile data on this peer, start processing requests
                // over the control stream.
                if peer_guard.discovered() && control_channel_task.is_none() {
                    trace!("state_watcher: Starting control stream task for peer {}", id);
                    control_channel_task = Some(start_control_stream_processing_task(peer.clone()));
                }

                if peer_guard.target_descriptor.is_some() && notification_poll_task.is_none() {
                    notification_poll_task =
                        Some(start_notifications_processing_task(peer.clone()));
                }
            }
        }

        match peer_guard.browse_channel.state() {
            &PeerChannelState::Connecting => {}
            &PeerChannelState::Disconnected => {
                browse_channel_task = None;
            }
            &PeerChannelState::Connected(_) => {
                // The Browse channel must be established after the control channel.
                // Ensure that the control channel exists before starting the browse task.
                if control_channel_task.is_none() {
                    trace!("Received Browse connection before Control .. disconnecting");
                    peer_guard.browse_channel.disconnect();
                    continue;
                }
                // We always use the newest Browse connection.
                browse_channel_task = Some(start_browse_stream_processing_task(peer.clone()));
            }
        }
    }

    trace!("state_watcher shutting down. aborting processors");

    let _ = make_connection_task.cancel();

    // Stop processing state changes on the browse channel.
    // This needs to happen before stopping the control channel.
    drop(browse_channel_task.take());

    // Stop processing state changes on the control channel.
    drop(control_channel_task.take());

    drop(notification_poll_task.take());
}
