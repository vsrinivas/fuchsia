// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use self::task::PeerTask;
use crate::{audio::AudioControl, config::AudioGatewayFeatureSupport, error::Error};
use {
    async_trait::async_trait,
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
    futures::{channel::mpsc, Future, FutureExt, SinkExt, TryFutureExt},
    parking_lot::Mutex,
    profile_client::ProfileEvent,
    std::sync::Arc,
};

#[cfg(test)]
use async_utils::event::{Event, EventWaitResult};

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
#[cfg_attr(test, derive(Debug))]
pub enum PeerRequest {
    Profile(ProfileEvent),
    Handle(PeerHandlerProxy),
    BatteryLevel(u8),
    Behavior(ConnectionBehavior),
}

#[derive(Debug, Clone, Copy)]
pub struct ConnectionBehavior {
    pub autoconnect: bool,
}

impl Default for ConnectionBehavior {
    fn default() -> Self {
        Self { autoconnect: true }
    }
}

impl From<fidl_fuchsia_bluetooth_hfp_test::ConnectionBehavior> for ConnectionBehavior {
    fn from(value: fidl_fuchsia_bluetooth_hfp_test::ConnectionBehavior) -> Self {
        let autoconnect = value.autoconnect.unwrap_or_else(|| Self::default().autoconnect);
        Self { autoconnect }
    }
}

/// Manages the Service Level Connection, Audio Connection, and FIDL APIs for a peer device.
#[async_trait]
pub trait Peer: Future<Output = PeerId> + Unpin + Send {
    fn id(&self) -> PeerId;

    /// Pass a new profile event into the Peer. The Peer can then react to the event as it sees
    /// fit. This method will return once the peer accepts the event.
    async fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error>;

    /// Create a FIDL channel that can be used to manage this Peer and return the server end.
    ///
    /// Returns an error if the fidl endpoints cannot be built or the request cannot be processed
    /// by the Peer.
    async fn build_handler(&mut self) -> Result<ServerEnd<PeerHandlerMarker>, Error>;

    /// Provide the `Peer` with the battery level of this device.
    /// `level` should be a value between 0-5 inclusive.
    async fn battery_level(&mut self, level: u8);

    /// Set the behavior used when connecting to remote peers.
    async fn set_connection_behavior(&mut self, behavior: ConnectionBehavior);
}

/// Concrete implementation for `Peer`.
pub struct PeerImpl {
    id: PeerId,
    local_config: AudioGatewayFeatureSupport,
    profile_proxy: ProfileProxy,
    connection_behavior: ConnectionBehavior,
    task: Task<()>,
    // A queue of all events destined for the peer.
    // Peer exposes methods for dealing with these various fidl requests in an easier fashion.
    // Under the hood, a queue is used to send these messages to the `task` which represents the
    // Peer.
    queue: mpsc::Sender<PeerRequest>,
    /// A handle to the audio control interface.
    audio_control: Arc<Mutex<Box<dyn AudioControl>>>,
}

impl PeerImpl {
    pub fn new(
        id: PeerId,
        profile_proxy: ProfileProxy,
        audio_control: Arc<Mutex<Box<dyn AudioControl>>>,
        local_config: AudioGatewayFeatureSupport,
        connection_behavior: ConnectionBehavior,
    ) -> Result<Self, Error> {
        let (task, queue) = PeerTask::spawn(
            id,
            profile_proxy.clone(),
            audio_control.clone(),
            local_config,
            connection_behavior,
        )?;
        Ok(Self {
            id,
            local_config,
            profile_proxy,
            task,
            audio_control,
            queue,
            connection_behavior,
        })
    }

    /// Spawn a new peer task.
    fn spawn_task(&mut self) -> Result<(), Error> {
        let (task, queue) = PeerTask::spawn(
            self.id,
            self.profile_proxy.clone(),
            self.audio_control.clone(),
            self.local_config,
            self.connection_behavior,
        )?;
        self.task = task;
        self.queue = queue;
        Ok(())
    }

    /// Method completes when a peer task accepts the request.
    ///
    /// Panics if the PeerTask was not able to receive the request.
    /// This should only be used when it is expected that the peer can receive the request and
    /// it is a critical failure when it does not receive the request.
    fn expect_send_request(&mut self, request: PeerRequest) -> impl Future<Output = ()> + '_ {
        self.queue
            .send(request)
            .unwrap_or_else(|e| panic!("PeerTask should be running and processing: got {:?}", e))
    }
}

#[async_trait]
impl Peer for PeerImpl {
    fn id(&self) -> PeerId {
        self.id
    }

    /// This method will panic if the peer cannot accept a profile event. This is not expected to
    /// happen under normal operation and likely indicates a bug or unrecoverable failure condition
    /// in the system.
    async fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        // The fuchsia.bluetooth.bredr Profile APIs ultimately control the creation of Peers.
        // Therefore, they will recreate the peer task if it is not running.
        if let Err(request) = self.queue.try_send_fut(PeerRequest::Profile(event)).await {
            // Task ended, so let's spin it back up since somebody wants it.
            self.spawn_task()?;
            self.expect_send_request(request).await;
        }
        Ok(())
    }

    async fn build_handler(&mut self) -> Result<ServerEnd<PeerHandlerMarker>, Error> {
        let (proxy, server_end) = fidl::endpoints::create_proxy()
            .map_err(|e| Error::system("Could not create call manager fidl endpoints", e))?;
        self.queue
            .try_send_fut(PeerRequest::Handle(proxy))
            .await
            .map_err(|_| Error::PeerRemoved)?;
        Ok(server_end)
    }

    async fn battery_level(&mut self, level: u8) {
        let _ = self.queue.try_send_fut(PeerRequest::BatteryLevel(level)).await;
    }

    async fn set_connection_behavior(&mut self, behavior: ConnectionBehavior) {
        self.connection_behavior = behavior;
        let _ = self.queue.try_send_fut(PeerRequest::Behavior(behavior)).await;
    }
}

impl Future for PeerImpl {
    type Output = PeerId;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.task.poll_unpin(cx).map(|()| self.id)
    }
}

#[cfg(test)]
pub(crate) mod fake {
    use super::*;

    /// Receives data from the fake peer's channel. Notifies PeerFake when dropped.
    pub struct PeerFakeReceiver {
        /// Use `receiver` to receive messages.
        pub receiver: mpsc::Receiver<PeerRequest>,
        _close: Event,
    }

    /// A fake Peer implementation which sends messages on the channel.
    /// PeerFake as a Future completes when PeerFakeReceiver is dropped.
    pub struct PeerFake {
        id: PeerId,
        queue: mpsc::Sender<PeerRequest>,
        closed: EventWaitResult,
    }

    impl PeerFake {
        pub fn new(id: PeerId) -> (PeerFakeReceiver, Self) {
            let (queue, receiver) = mpsc::channel(1);
            let close = Event::new();
            let closed = close.wait_or_dropped();
            (PeerFakeReceiver { receiver, _close: close }, Self { id, queue, closed })
        }

        async fn expect_send_request(&mut self, request: PeerRequest) {
            self.queue
                .send(request)
                .await
                .expect("PeerTask to be running and able to process requests");
        }
    }

    #[async_trait]
    impl Peer for PeerFake {
        fn id(&self) -> PeerId {
            self.id
        }

        async fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
            self.expect_send_request(PeerRequest::Profile(event)).await;
            Ok(())
        }

        async fn build_handler(&mut self) -> Result<ServerEnd<PeerHandlerMarker>, Error> {
            unimplemented!("Not needed for any currently written tests");
        }

        async fn battery_level(&mut self, level: u8) {
            self.expect_send_request(PeerRequest::BatteryLevel(level)).await;
        }

        async fn set_connection_behavior(&mut self, behavior: ConnectionBehavior) {
            self.expect_send_request(PeerRequest::Behavior(behavior)).await;
        }
    }

    impl Future for PeerFake {
        type Output = PeerId;

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            self.closed.poll_unpin(cx).map(|_| self.id)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, async_utils::PollExt, fidl_fuchsia_bluetooth_bredr::ProfileMarker,
        fuchsia_async as fasync, futures::pin_mut, matches::assert_matches,
    };

    use crate::audio::TestAudioControl;

    fn new_audio_control() -> Arc<Mutex<Box<dyn AudioControl>>> {
        Arc::new(Mutex::new(Box::new(TestAudioControl::default())))
    }

    #[test]
    fn peer_id_returns_expected_id() {
        // TestExecutor must exist in order to create fidl endpoints
        let _exec = fasync::TestExecutor::new().unwrap();

        let id = PeerId(1);
        let proxy = fidl::endpoints::create_proxy::<ProfileMarker>().unwrap().0;
        let peer = PeerImpl::new(
            id,
            proxy,
            new_audio_control(),
            AudioGatewayFeatureSupport::default(),
            ConnectionBehavior::default(),
        )
        .unwrap();
        assert_eq!(peer.id(), id);
    }

    #[test]
    fn profile_event_request_respawns_task_successfully() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let id = PeerId(1);
        let proxy = fidl::endpoints::create_proxy::<ProfileMarker>().unwrap().0;
        let mut peer = PeerImpl::new(
            id,
            proxy,
            new_audio_control(),
            AudioGatewayFeatureSupport::default(),
            ConnectionBehavior::default(),
        )
        .unwrap();

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
        let mut peer = PeerImpl::new(
            id,
            proxy,
            new_audio_control(),
            AudioGatewayFeatureSupport::default(),
            ConnectionBehavior::default(),
        )
        .unwrap();

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
