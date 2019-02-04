// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_error::log_error_discard_result;
use crate::service::ServiceEvent;
use crate::{Result, CHANNEL_BUFFER_SIZE};
use failure::ResultExt;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_mediasession::{
    ControllerControlHandle, ControllerEvent, ControllerEventStream, ControllerMarker,
    ControllerProxy, ControllerRequest, PlaybackState,
};
use fuchsia_async as fasync;
use futures::{
    channel::mpsc::{channel, Receiver, Sender},
    future, select, Future, FutureExt, SinkExt, StreamExt, TryFutureExt, TryStreamExt,
};

/// `Session` multiplexes the `fuchsia.mediasession.Controller` implementation of
/// a published media session.
pub struct Session {
    id: u64,
    event_broadcaster: EventBroadcaster,
    request_forwarder: RequestForwarder,
    service_event_sink: Sender<ServiceEvent>,
}

impl Session {
    /// Creates a new `Session` which multiplexes `controller_proxy`.
    ///
    /// `Session` should not be used after it sends a `SessionClosed`
    /// `ServiceEvent`.
    pub fn new(
        id: u64,
        controller_proxy: ControllerProxy,
        service_event_sink: Sender<ServiceEvent>,
    ) -> Result<Self> {
        let event_stream = controller_proxy.take_event_stream();
        Ok(Self {
            id,
            event_broadcaster: EventBroadcaster::new(id, service_event_sink.clone(), event_stream),
            request_forwarder: RequestForwarder::new(controller_proxy),
            service_event_sink,
        })
    }

    pub fn id(&self) -> u64 {
        self.id
    }

    pub async fn serve(self, mut fidl_requests: Receiver<ServerEnd<ControllerMarker>>) {
        let (forwarder_handle, forwarder_registration) = future::AbortHandle::new_pair();
        let request_sink = self.request_forwarder.start(forwarder_registration);
        let mut service_event_sink = self.service_event_sink;
        let session_id = self.id;
        let mut listener_sink = self.event_broadcaster.start(
            async move {
                forwarder_handle.abort();
                trylog!(await!(service_event_sink.send(ServiceEvent::SessionClosed(session_id)))
                    .context("Sending Session epitaph."));
            },
        );

        // Connect all new clients to the request forwarding and event broadcasting tasks.
        while let Some(new_client) = await!(fidl_requests.next()) {
            let request_sink = request_sink.clone();
            let (request_stream, control_handle) = trylog!(new_client
                .into_stream_and_control_handle()
                .context("Converting client to request stream."));

            // Send the control handle to the event broadcasting task.
            trylog!(await!(listener_sink.send(control_handle)));

            // Forward the request stream to the request serving task.
            fasync::spawn(
                request_stream
                    .filter_map(|result| {
                        future::ready(match result {
                            Ok(event) => Some(Ok(event)),
                            Err(_) => None,
                        })
                    })
                    .forward(request_sink)
                    .map(log_error_discard_result),
            );
        }
    }
}

/// Forwards requests from all proxied clients to the backing
/// `fuchsia.mediasession.Controller` implementation.
struct RequestForwarder {
    controller_proxy: ControllerProxy,
}

impl RequestForwarder {
    fn new(controller_proxy: ControllerProxy) -> Self {
        Self { controller_proxy }
    }

    fn start(self, abort_registration: future::AbortRegistration) -> Sender<ControllerRequest> {
        let (request_sink, request_stream) = channel(CHANNEL_BUFFER_SIZE);
        let serve_fut = future::Abortable::new(self.serve(request_stream), abort_registration)
            .unwrap_or_else(|_| {
                // This is just an abort message; ignore it.
                // This happens when the backing implementation disconnects and is not an error.
            });
        fasync::spawn(serve_fut);
        request_sink
    }

    async fn serve(self, mut request_stream: Receiver<ControllerRequest>) {
        while let Some(request) = await!(request_stream.next()) {
            trylog!(self.serve_request_with_controller(request));
        }
    }

    /// Forwards a single request to the `ControllerProxy`.
    fn serve_request_with_controller(&self, request: ControllerRequest) -> Result<()> {
        match request {
            ControllerRequest::Play { .. } => self.controller_proxy.play()?,
            ControllerRequest::Pause { .. } => self.controller_proxy.pause()?,
            ControllerRequest::Stop { .. } => self.controller_proxy.stop()?,
            ControllerRequest::SeekToPosition { position, .. } => {
                self.controller_proxy.seek_to_position(position)?
            }
            ControllerRequest::SkipForward { skip_amount, .. } => {
                self.controller_proxy.skip_forward(skip_amount)?
            }
            ControllerRequest::SkipReverse { skip_amount, .. } => {
                self.controller_proxy.skip_reverse(skip_amount)?
            }
            ControllerRequest::NextItem { .. } => self.controller_proxy.next_item()?,
            ControllerRequest::PrevItem { .. } => self.controller_proxy.prev_item()?,
            ControllerRequest::SetPlaybackRate { playback_rate, .. } => {
                self.controller_proxy.set_playback_rate(playback_rate)?
            }
            ControllerRequest::SetRepeatMode { repeat_mode, .. } => {
                self.controller_proxy.set_repeat_mode(repeat_mode)?
            }
            ControllerRequest::SetShuffleMode { shuffle_on, .. } => {
                self.controller_proxy.set_shuffle_mode(shuffle_on)?
            }
            ControllerRequest::BindGainControl { gain_control_request, .. } => {
                self.controller_proxy.bind_gain_control(gain_control_request)?
            }
            ControllerRequest::ConnectToExtension { extension, channel, .. } => {
                self.controller_proxy.connect_to_extension(&extension, channel)?
            }
        };
        Ok(())
    }
}

/// Stores the most recent events sent by the backing
/// `fuchsia.mediasession.Controller` implementation which represent the state of
/// the media session.
#[derive(Default)]
struct SessionState {
    metadata: Option<ControllerEvent>,
    playback_capabilities: Option<ControllerEvent>,
    playback_status: Option<ControllerEvent>,
}

impl SessionState {
    fn new() -> Self {
        Default::default()
    }

    /// Updates the stored state with the new `event`.
    fn update(&mut self, event: ControllerEvent) {
        let to_update = match &event {
            ControllerEvent::OnPlaybackStatusChanged { .. } => &mut self.playback_status,
            ControllerEvent::OnMetadataChanged { .. } => &mut self.metadata,
            ControllerEvent::OnPlaybackCapabilitiesChanged { .. } => {
                &mut self.playback_capabilities
            }
        };
        *to_update = Some(event);
    }
}

/// Broadcasts events from the backing `Controller` implementation to all proxied
/// clients and, if it is qualifying activity, to the Media Session service so it
/// can track active sessions.
struct EventBroadcaster {
    id: u64,
    service_event_sink: Sender<ServiceEvent>,
    source: ControllerEventStream,
}

impl EventBroadcaster {
    fn new(
        id: u64,
        service_event_sink: Sender<ServiceEvent>,
        source: ControllerEventStream,
    ) -> Self {
        Self { id, service_event_sink, source }
    }

    fn start(
        self,
        epitaph: impl Future<Output = ()> + Send + 'static,
    ) -> Sender<ControllerControlHandle> {
        let (listener_sink, listener_stream) = channel(CHANNEL_BUFFER_SIZE);
        fasync::spawn(
            async move {
                await!(self.serve(listener_stream));
                await!(epitaph);
            },
        );
        listener_sink
    }

    /// Continuously broadcasts events from the `source` to listeners
    /// received over the `listener_stream`.
    ///
    /// New listeners are pushed the most recent state from before their
    /// connection.
    ///
    /// Event listeners are dropped as their client ends disconnect.
    async fn serve(mut self, mut listener_stream: Receiver<ControllerControlHandle>) {
        let mut session_state = SessionState::new();
        let mut listeners = Vec::new();

        loop {
            select! {
                event = self.source.try_next() => {
                    let event = trylogbreak!(event);
                    match event {
                        Some(mut event) => {
                            if Self::event_is_an_active_playback_status(&event) {
                                trylogbreak!(await!(self.service_event_sink.send(ServiceEvent::SessionActivity(self.id))));
                            }
                            Self::broadcast_event(&mut event, &mut listeners);
                            session_state.update(event);
                        },
                        None => break,
                    };
                }
                listener = listener_stream.next() => {
                    match listener.map(|listener| {
                        Self::deliver_state(&mut session_state, &listener).map(|_| listener)
                    }) {
                        Some(Ok(listener)) => listeners.push(listener),
                        Some(Err(e)) => eprintln!("Failed to push state to new event listener: {}.", e),
                        _ => (),
                    }
                }
                complete => {
                    break
                },
            }
        }

        for listener in listeners.into_iter() {
            listener.shutdown();
        }
    }

    /// Broadcasts an event to all event listeners by control handle.
    ///
    /// Only connected event listeners are retained.
    ///
    /// This will not work for events which have handles as they can only be sent
    /// once.
    fn broadcast_event(event: &mut ControllerEvent, listeners: &mut Vec<ControllerControlHandle>) {
        listeners.retain(|listener| Self::send_event(listener, event).is_ok());
    }

    /// Sends an event to a listener by a control handle.
    fn send_event(listener: &ControllerControlHandle, event: &mut ControllerEvent) -> Result<()> {
        match event {
            ControllerEvent::OnPlaybackStatusChanged { playback_status } => {
                listener.send_on_playback_status_changed(playback_status)
            }
            ControllerEvent::OnMetadataChanged { media_metadata } => {
                listener.send_on_metadata_changed(media_metadata)
            }
            ControllerEvent::OnPlaybackCapabilitiesChanged { playback_capabilities } => {
                listener.send_on_playback_capabilities_changed(playback_capabilities)
            }
        }
        .map_err(Into::into)
    }

    /// Delivers `state` to `listener`.
    fn deliver_state(state: &mut SessionState, listener: &ControllerControlHandle) -> Result<()> {
        [&mut state.metadata, &mut state.playback_capabilities, &mut state.playback_status]
            .iter_mut()
            .filter_map(|update: &mut &mut Option<ControllerEvent>| (*update).as_mut())
            .map(|update: &mut ControllerEvent| Self::send_event(&listener, update))
            .collect()
    }

    fn event_is_an_active_playback_status(event: &ControllerEvent) -> bool {
        match *event {
            ControllerEvent::OnPlaybackStatusChanged { ref playback_status }
                if playback_status.playback_state == PlaybackState::Playing =>
            {
                true
            }
            _ => false,
        }
    }
}
