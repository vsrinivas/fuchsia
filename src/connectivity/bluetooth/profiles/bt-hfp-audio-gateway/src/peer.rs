// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use self::task::PeerTask;
use crate::{config::AudioGatewayFeatureSupport, error::Error};
use {
    async_utils::channel::TrySend,
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_bluetooth_bredr::ProfileProxy,
    fidl_fuchsia_bluetooth_hfp::{PeerHandlerMarker, PeerHandlerProxy},
    fuchsia_async::Task,
    fuchsia_bluetooth::types::PeerId,
    futures::{channel::mpsc, Future, FutureExt, SinkExt},
    profile_client::ProfileEvent,
};

pub mod calls;
pub mod gain_control;
pub mod indicators;
pub mod procedure;
mod ringer;
pub mod service_level_connection;
pub mod slc_request;
mod task;
pub mod update;

/// A request made to the Peer that should be passed along to the PeerTask
enum PeerRequest {
    Profile(ProfileEvent),
    Handle(PeerHandlerProxy),
    BatteryLevel(u8),
}

/// Represents a Bluetooth peer that implements the HFP Hands Free role.
/// Peer implements Future which completes when the peer is removed from the system
/// and should be cleaned up.
pub struct Peer {
    id: PeerId,
    local_config: AudioGatewayFeatureSupport,
    profile_proxy: ProfileProxy,
    task: Task<()>,
    // A queue of all possible fidl requests destined for the peer.
    // Peer exposes methods for dealing with these various fidl requests in an easier fashion.
    // Under the hood, a queue is used to send these messages to the `task` which represents the
    // Peer.
    queue: mpsc::Sender<PeerRequest>,
}

impl Peer {
    pub fn new(
        id: PeerId,
        profile_proxy: ProfileProxy,
        local_config: AudioGatewayFeatureSupport,
    ) -> Result<Self, Error> {
        let (task, queue) = PeerTask::spawn(id, profile_proxy.clone(), local_config)?;
        Ok(Self { id, local_config, profile_proxy, task, queue })
    }

    pub fn id(&self) -> PeerId {
        self.id
    }

    /// Spawn a new peer task.
    fn spawn_task(&mut self) -> Result<(), Error> {
        let (task, queue) =
            PeerTask::spawn(self.id, self.profile_proxy.clone(), self.local_config)?;
        self.task = task;
        self.queue = queue;
        Ok(())
    }

    /// Pass a new profile event into the Peer. The Peer can then react to the event as it sees
    /// fit.
    /// This method will return once the peer accepts the event.
    ///
    /// This method will panic if the peer cannot accept a profile event. This is not expected to
    /// happen under normal operation and likely indicates a bug or unrecoverable failure condition
    /// in the system
    pub async fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        // The fuchsia.bluetooth.bredr Profile APIs ultimately control the creation of Peers.
        // Therefore, they will recreate the peer task if it is not running.
        if let Err(request) = self.queue.try_send_fut(PeerRequest::Profile(event)).await {
            // Task ended, so let's spin it back up since somebody wants it.
            self.spawn_task()?;
            self.expect_send_request(request).await;
        }
        Ok(())
    }

    /// Create a FIDL channel that can be used to manage this Peer and return the server end.
    ///
    /// Returns an error if the fidl endpoints cannot be built or the request cannot be processed
    /// by the Peer.
    pub async fn build_handler(&mut self) -> Result<ServerEnd<PeerHandlerMarker>, Error> {
        let (proxy, server_end) = fidl::endpoints::create_proxy()
            .map_err(|e| Error::system("Could not create call manager fidl endpoints", e))?;
        self.queue
            .try_send_fut(PeerRequest::Handle(proxy))
            .await
            .map_err(|_| Error::PeerRemoved)?;
        Ok(server_end)
    }

    pub async fn battery_level(&mut self, level: u8) {
        let _ = self.queue.try_send_fut(PeerRequest::BatteryLevel(level)).await;
    }

    /// Method completes when a peer task accepts the request.
    ///
    /// Panics if the PeerTask was not able to receive the request.
    /// This should only be used when it is expected that the peer can receive the request and
    /// it is a critical failure when it does not receive the request.
    async fn expect_send_request(&mut self, request: PeerRequest) {
        self.queue
            .send(request)
            .await
            .expect("PeerTask to be running and able to process requests");
    }
}

impl Future for Peer {
    type Output = PeerId;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.task.poll_unpin(cx).map(|()| self.id)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, async_utils::PollExt, fidl_fuchsia_bluetooth_bredr::ProfileMarker,
        fuchsia_async as fasync, futures::pin_mut, matches::assert_matches,
    };

    #[test]
    fn peer_id_returns_expected_id() {
        // TestExecutor must exist in order to create fidl endpoints
        let _exec = fasync::TestExecutor::new().unwrap();

        let id = PeerId(1);
        let proxy = fidl::endpoints::create_proxy::<ProfileMarker>().unwrap().0;
        let peer = Peer::new(id, proxy, AudioGatewayFeatureSupport::default()).unwrap();
        assert_eq!(peer.id(), id);
    }

    #[test]
    fn profile_event_request_respawns_task_successfully() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let id = PeerId(1);
        let proxy = fidl::endpoints::create_proxy::<ProfileMarker>().unwrap().0;
        let mut peer = Peer::new(id, proxy, AudioGatewayFeatureSupport::default()).unwrap();

        // Stop the inner task and wait until it has fully stopped
        // The inner task is replaced by a no-op task so that it can be consumed and canceled.
        let task = std::mem::replace(&mut peer.task, fasync::Task::local(async move {}));
        let cancelation = task.cancel();
        pin_mut!(cancelation);
        exec.run_until_stalled(&mut cancelation).expect("task to stop completely");

        // create profile_event_fut in a block to limit its lifetime
        {
            let event =
                ProfileEvent::SearchResult { id: PeerId(1), protocol: None, attributes: vec![] };
            let profile_event_fut = peer.profile_event(event);
            pin_mut!(profile_event_fut);
            exec.run_until_stalled(&mut profile_event_fut)
                .expect("Profile Event to complete")
                .expect("Profile Event to succeed");
        }

        // A new task has been spun up and is actively running.
        let task = std::mem::replace(&mut peer.task, fasync::Task::local(async move {}));
        pin_mut!(task);
        assert!(exec.run_until_stalled(&mut task).is_pending());
    }

    #[test]
    fn manager_request_returns_error_when_task_is_stopped() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let id = PeerId(1);
        let proxy = fidl::endpoints::create_proxy::<ProfileMarker>().unwrap().0;
        let mut peer = Peer::new(id, proxy, AudioGatewayFeatureSupport::default()).unwrap();

        // Stop the inner task and wait until it has fully stopped
        // The inner task is replaced by a no-op task so that it can be consumed and canceled.
        let cancelation =
            std::mem::replace(&mut peer.task, fasync::Task::local(async move {})).cancel();
        pin_mut!(cancelation);
        exec.run_until_stalled(&mut cancelation).expect("task to stop completely");

        let build_handler_fut = peer.build_handler();
        pin_mut!(build_handler_fut);
        let result = exec
            .run_until_stalled(&mut build_handler_fut)
            .expect("Manager handler registration to complete successfully");
        assert_matches!(result, Err(Error::PeerRemoved));
    }
}
