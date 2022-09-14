// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{create_proxy, Proxy},
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_composition as ui_comp,
    futures::future::AbortHandle,
};

use crate::{
    event::{ChildViewEvent, Event, ViewSpecHolder},
    utils::{spawn_abortable, EventSender},
    window::WindowId,
};

/// Defines a type to hold an id to a child view. This implementation uses the value of
/// [ViewportCreationToken] to be the child view id.
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ChildViewId(u64);

impl ChildViewId {
    pub fn from_viewport_content_id(viewport: ui_comp::ContentId) -> Self {
        ChildViewId(viewport.value)
    }
}

/// Defines a struct to hold state for ChildView.
#[derive(Debug)]
pub struct ChildView<T> {
    viewport_content_id: ui_comp::ContentId,
    _window_id: WindowId,
    _event_sender: EventSender<T>,
    services_abort: Option<AbortHandle>,
}

impl<T> Drop for ChildView<T> {
    fn drop(&mut self) {
        self.services_abort.take().map(|a| a.abort());
    }
}

impl<T> ChildView<T> {
    pub(crate) fn new(
        flatland: ui_comp::FlatlandProxy,
        window_id: WindowId,
        viewport_content_id: ui_comp::ContentId,
        view_spec_holder: ViewSpecHolder,
        width: u32,
        height: u32,
        event_sender: EventSender<T>,
    ) -> Result<Self, Error>
    where
        T: 'static + Sync + Send,
    {
        let mut viewport_creation_token = match view_spec_holder.view_spec.viewport_creation_token {
            Some(token) => token,
            None => {
                return Err(format_err!("Ignoring non-flatland client's attempt to present."));
            }
        };

        let (child_view_watcher_proxy, child_view_watcher_request) =
            create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

        flatland.create_viewport(
            &mut viewport_content_id.clone(),
            &mut viewport_creation_token,
            ui_comp::ViewportProperties {
                logical_size: Some(fmath::SizeU { width, height }),
                ..ui_comp::ViewportProperties::EMPTY
            },
            child_view_watcher_request,
        )?;

        view_spec_holder.responder.send(&mut Ok(()))?;

        let child_view_id = ChildViewId::from_viewport_content_id(viewport_content_id);
        let child_view_watcher_fut = Self::start_child_view_watcher(
            child_view_watcher_proxy,
            child_view_id,
            window_id,
            event_sender.clone(),
        );

        let abort_handle = spawn_abortable(child_view_watcher_fut);
        let services_abort = Some(abort_handle);

        Ok(ChildView {
            viewport_content_id,
            _window_id: window_id,
            _event_sender: event_sender,
            services_abort,
        })
    }

    pub fn get_content_id(&self) -> ui_comp::ContentId {
        self.viewport_content_id.clone()
    }

    pub fn id(&self) -> ChildViewId {
        ChildViewId::from_viewport_content_id(self.viewport_content_id)
    }

    async fn start_child_view_watcher(
        child_view_watcher_proxy: ui_comp::ChildViewWatcherProxy,
        child_view_id: ChildViewId,
        window_id: WindowId,
        event_sender: EventSender<T>,
    ) {
        if let Ok(_) = child_view_watcher_proxy.get_status().await {
            event_sender
                .send(Event::ChildViewEvent(child_view_id, window_id, ChildViewEvent::Available))
                .expect("Failed to send ChildView::Available event");
        }
        if let Ok(view_ref) = child_view_watcher_proxy.get_view_ref().await {
            event_sender
                .send(Event::ChildViewEvent(
                    child_view_id,
                    window_id,
                    ChildViewEvent::Attached(view_ref),
                ))
                .expect("Failed to send ChildView::Attached event");
        }

        // After retrieving status and viewRef, we can only wait for the channel to close. This is a
        // useful signal when the child view's component exits or crashes or does not use
        // [felement::ViewController]'s dismiss method.
        let _ = child_view_watcher_proxy.on_closed().await;
        event_sender
            .send(Event::ChildViewEvent(child_view_id, window_id, ChildViewEvent::Detached))
            .expect("Failed to send ChildView::Detached event");
    }
}
