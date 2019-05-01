// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    active_session_queue::ActiveSessionQueue, fidl_clones::*, log_error::log_error_discard_result,
    mpmc, session_list::SessionList, Result, CHANNEL_BUFFER_SIZE,
};
use fidl::encoding::OutOfLine;
use fidl::endpoints::*;
use fidl_fuchsia_media::Metadata;
use fidl_fuchsia_media_sessions::*;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{future::try_join, lock::Mutex, Future, FutureExt, StreamExt, TryStreamExt};
use std::{
    collections::HashMap,
    ops::{Deref, DerefMut},
    sync::Arc,
};

#[derive(Clone, Debug)]
pub struct SessionRegistration {
    pub id: Arc<zx::Event>,
    pub koid: zx::Koid,
    pub is_local: bool,
}

/// `Session` is the in-process proxy to a media session.
#[derive(Clone)]
pub struct Session {
    proxy: Arc<SessionProxy>,
    state: Arc<Mutex<SessionState>>,
    events: mpmc::Receiver<Clonable<SessionEvent>>,
    cancel_signal: mpmc::Receiver<()>,
}

#[derive(Debug, Clone)]
pub enum SessionCollectionEvent {
    Added,
    Removed,
}

impl Session {
    pub async fn serve(
        client_end: ClientEnd<SessionMarker>,
        registration: SessionRegistration,
        active_session_queue: Arc<Mutex<ActiveSessionQueue>>,
        session_list: Arc<Mutex<SessionList>>,
        mut collection_event_sink: mpmc::Sender<(SessionRegistration, SessionCollectionEvent)>,
        mut active_session_sink: mpmc::Sender<Option<SessionRegistration>>,
    ) -> Result<Self> {
        let proxy = client_end.into_proxy()?;
        let mut event_stream = proxy.take_event_stream();
        let (mut event_sender, event_receiver) = mpmc::channel(CHANNEL_BUFFER_SIZE);
        let (mut cancel_signaller, cancel_signal) = mpmc::channel(1);
        let state = Arc::new(Mutex::new(SessionState::default()));
        let session = Session {
            proxy: Arc::new(proxy),
            events: event_receiver,
            state: state.clone(),
            cancel_signal,
        };
        fasync::spawn(
            async move {
                while let Ok(Some(event)) = await!(event_stream.try_next()) {
                    if is_active_status(&event) && registration.is_local {
                        let ref mut queue = await!(active_session_queue.lock());
                        let active_session_changed = queue.promote_session(registration.clone());
                        if active_session_changed {
                            await!(active_session_sink.send(queue.active_session()));
                        }
                    }
                    await!(state.lock()).deref_mut().update(&event);
                    await!(event_sender.send(Clonable(event)));
                }
                await!(session_list.lock()).deref_mut().remove(registration.koid);
                await!(active_session_queue.lock()).deref_mut().remove_session(&registration);
                await!(collection_event_sink
                    .send((registration.clone(), SessionCollectionEvent::Removed)));
                await!(cancel_signaller.send(()));
            },
        );
        Ok(session)
    }

    pub async fn connect(&self, server_end: ServerEnd<SessionMarker>) -> Result<()> {
        let (request_stream, control_handle) = server_end.into_stream_and_control_handle()?;
        let events_to_catch_up_client = await!(self.state.lock()).deref().events();
        for event in events_to_catch_up_client {
            if Self::send_event(&control_handle, &event).is_err() {
                // Client is disconnected.
                return Ok(());
            }
        }

        fasync::spawn(try_join(
            self.request_forwarder(request_stream),
            self.event_forwarder(control_handle),
        ).map(log_error_discard_result));
        Ok(())
    }

    fn request_forwarder(
        &self,
        mut request_stream: SessionRequestStream,
    ) -> impl Future<Output = Result<()>> {
        let proxy = self.proxy.clone();
        let mut cancel_signal = self.cancel_signal.clone();
        async move {
            let mut cancel_signal = cancel_signal.next().fuse();
            loop {
                futures::select! {
                    request = request_stream.select_next_some() => {
                        await!(Self::serve_request(proxy.deref(), request?))?;
                    },
                    _cancel = cancel_signal => {
                        break;
                    }
                }
            }
            Ok(())
        }
    }

    fn event_forwarder(
        &self,
        control_handle: SessionControlHandle,
    ) -> impl Future<Output = Result<()>> {
        let mut event_stream = self.events.clone();
        let mut cancel_signal = self.cancel_signal.clone();
        async move {
            let mut cancel_signal = cancel_signal.next().fuse();
            loop {
                futures::select! {
                    Clonable(event) = event_stream.select_next_some() => {
                        Self::send_event(&control_handle, &event)?;
                    }
                    _cancel = cancel_signal => {
                        break;
                    }
                }
            }
            Ok(())
        }
    }

    fn send_event(control_handle: &SessionControlHandle, event: &SessionEvent) -> Result<()> {
        Ok(match event {
            SessionEvent::OnPlaybackStatusChanged { playback_status } => control_handle
                .send_on_playback_status_changed(clone_playback_status(playback_status)),
            SessionEvent::OnMetadataChanged { media_metadata } => {
                control_handle.send_on_metadata_changed(&mut clone_metadata(media_metadata))
            }
            SessionEvent::OnPlaybackCapabilitiesChanged { playback_capabilities } => control_handle
                .send_on_playback_capabilities_changed(clone_playback_capabilities(
                    playback_capabilities,
                )),
            SessionEvent::OnMediaImagesChanged { media_images } => {
                let mut images: Vec<MediaImage> =
                    media_images.iter().map(clone_media_image).collect();
                control_handle.send_on_media_images_changed(&mut images.iter_mut())
            }
        }?)
    }

    async fn serve_request(proxy: &SessionProxy, request: SessionRequest) -> Result<()> {
        match request {
            SessionRequest::Play { .. } => proxy.play()?,
            SessionRequest::Pause { .. } => proxy.pause()?,
            SessionRequest::Stop { .. } => proxy.stop()?,
            SessionRequest::SeekToPosition { position, .. } => proxy.seek_to_position(position)?,
            SessionRequest::SkipForward { skip_amount, .. } => proxy.skip_forward(skip_amount)?,
            SessionRequest::SkipReverse { skip_amount, .. } => proxy.skip_reverse(skip_amount)?,
            SessionRequest::NextItem { .. } => proxy.next_item()?,
            SessionRequest::PrevItem { .. } => proxy.prev_item()?,
            SessionRequest::SetPlaybackRate { playback_rate, .. } => {
                proxy.set_playback_rate(playback_rate)?
            }
            SessionRequest::SetRepeatMode { repeat_mode, .. } => {
                proxy.set_repeat_mode(repeat_mode)?
            }
            SessionRequest::SetShuffleMode { shuffle_on, .. } => {
                proxy.set_shuffle_mode(shuffle_on)?
            }
            SessionRequest::BindGainControl { gain_control_request, .. } => {
                proxy.bind_gain_control(gain_control_request)?
            }
            SessionRequest::ConnectToExtension { extension, channel, .. } => {
                proxy.connect_to_extension(&extension, channel)?
            }
            SessionRequest::GetMediaImageBitmap {
                url,
                mut minimum_size,
                mut desired_size,
                responder,
            } => {
                let mut bitmap = await!(proxy.get_media_image_bitmap(
                    &url,
                    &mut minimum_size,
                    &mut desired_size
                ))?;
                let response = bitmap.as_mut().map(|b| OutOfLine(b.deref_mut()));
                responder.send(response)?
            }
        };
        Ok(())
    }
}

/// `SessionState` keeps the last advertised state from each of a session's event
/// streams so that new clients can be caught up when they connect.
#[derive(Debug, Default)]
struct SessionState {
    playback_status: Option<PlaybackStatus>,
    playback_capabilities: Option<PlaybackCapabilities>,
    media_metadata: Option<Metadata>,
    media_images: HashMap<MediaImageType, MediaImage>,
}

impl Clone for SessionState {
    fn clone(&self) -> Self {
        Self {
            playback_status: self.playback_status.as_ref().map(clone_playback_status),
            playback_capabilities: self
                .playback_capabilities
                .as_ref()
                .map(clone_playback_capabilities),
            media_metadata: self.media_metadata.as_ref().map(clone_metadata),
            media_images: self
                .media_images
                .iter()
                .map(|(image_type, image)| (*image_type, clone_media_image(image)))
                .collect(),
        }
    }
}

impl SessionState {
    /// Returns the state with each field represented as an event.
    pub fn events(&self) -> Vec<SessionEvent> {
        let mut events = Vec::new();

        if let Some(event) = self
            .playback_status
            .as_ref()
            .map(clone_playback_status)
            .map(|playback_status| SessionEvent::OnPlaybackStatusChanged { playback_status })
        {
            events.push(event);
        }

        if let Some(event) =
            self.playback_capabilities.as_ref().map(clone_playback_capabilities).map(
                |playback_capabilities| SessionEvent::OnPlaybackCapabilitiesChanged {
                    playback_capabilities,
                },
            )
        {
            events.push(event);
        }

        if let Some(event) = self
            .media_metadata
            .as_ref()
            .map(clone_metadata)
            .map(|media_metadata| SessionEvent::OnMetadataChanged { media_metadata })
        {
            events.push(event);
        }

        // We don't want to send an empty list of media images on first connection.
        if !self.media_images.is_empty() {
            events.push(SessionEvent::OnMediaImagesChanged {
                media_images: self.media_images.values().map(clone_media_image).collect(),
            });
        }

        events
    }

    fn update(&mut self, event: &SessionEvent) {
        match event {
            SessionEvent::OnPlaybackStatusChanged { ref playback_status } => {
                self.playback_status.get_or_insert_with(|| clone_playback_status(playback_status));
            }
            SessionEvent::OnMetadataChanged { ref media_metadata } => {
                self.media_metadata.get_or_insert_with(|| clone_metadata(media_metadata));
            }
            SessionEvent::OnPlaybackCapabilitiesChanged { ref playback_capabilities } => {
                self.playback_capabilities
                    .get_or_insert_with(|| clone_playback_capabilities(playback_capabilities));
            }
            SessionEvent::OnMediaImagesChanged { media_images } => {
                for image in media_images {
                    self.media_images
                        .entry(image.image_type)
                        .or_insert_with(|| clone_media_image(image));
                }
            }
        }
    }
}

fn is_active_status(event: &SessionEvent) -> bool {
    match event {
        SessionEvent::OnPlaybackStatusChanged {
            playback_status: PlaybackStatus { playback_state: Some(PlaybackState::Playing), .. },
            ..
        } => true,
        _ => false,
    }
}
