// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_diagnostics_types as fdiagnostics,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::oneshot,
        future::{BoxFuture, Shared},
        FutureExt, StreamExt,
    },
    std::ops::Deref,
};

/// Wrapper around the `ComponentControllerProxy` with utilities for handling events.
pub struct ComponentController {
    /// The wrapped `ComponentController` connection.
    inner: fcrunner::ComponentControllerProxy,

    /// Receiver for epitaphs coming from the connection.
    epitaph_value_recv: Shared<oneshot::Receiver<zx::Status>>,

    /// Receiver for diganostics data coming from an event.
    diagnostics_value_recv: Option<oneshot::Receiver<fdiagnostics::ComponentDiagnostics>>,

    /// The task listening for events.
    _event_listener_task: fasync::Task<()>,
}

impl Deref for ComponentController {
    type Target = fcrunner::ComponentControllerProxy;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<'a> ComponentController {
    /// Create a new wrapper around a `ComponentControllerProxy`.
    ///
    /// A task will be spawned to dispatch events on the `ComponentControllerProxy`.
    pub fn new(proxy: fcrunner::ComponentControllerProxy) -> Self {
        let (epitaph_sender, epitaph_value_recv) = oneshot::channel();
        let (diagnostics_sender, diagnostics_value_recv) = oneshot::channel();

        let event_stream = proxy.take_event_stream();
        let events_fut = Self::listen_for_events(event_stream, epitaph_sender, diagnostics_sender);
        let event_listener_task = fasync::Task::spawn(events_fut);

        Self {
            inner: proxy,
            epitaph_value_recv: epitaph_value_recv.shared(),
            diagnostics_value_recv: Some(diagnostics_value_recv),
            _event_listener_task: event_listener_task,
        }
    }

    /// Obtain a future for the epitaph value.
    ///
    /// If the async task handling the `ComponentControllerProxy` unexpectedly exits, the future
    /// will resolve to `PEER_CLOSED`. Otherwise, it will resolve to the `zx::Status` representing
    /// the epitaph sent on the channel.
    ///
    /// This method may be called multiple times from multiple threads.
    pub fn wait_for_epitaph(&self) -> BoxFuture<'static, zx::Status> {
        let val = self.epitaph_value_recv.clone();
        async move { val.await.unwrap_or(zx::Status::PEER_CLOSED) }.boxed()
    }

    /// Obtain a receiver for the diagnostics values sent by the runner for the component.
    ///
    /// This method may be called multiple times, but only the first call will obtain the value.
    pub fn take_diagnostics_receiver(
        &mut self,
    ) -> Option<oneshot::Receiver<fdiagnostics::ComponentDiagnostics>> {
        self.diagnostics_value_recv.take()
    }

    async fn listen_for_events(
        mut event_stream: fcrunner::ComponentControllerEventStream,
        epitaph_sender: oneshot::Sender<zx::Status>,
        diagnostics_sender: oneshot::Sender<fdiagnostics::ComponentDiagnostics>,
    ) {
        let mut epitaph_sender = Some(epitaph_sender);
        let mut diagnostics_sender = Some(diagnostics_sender);
        while let Some(value) = event_stream.next().await {
            match value {
                Err(fidl::Error::ClientChannelClosed { status, .. }) => {
                    epitaph_sender.take().and_then(|sender| sender.send(status).ok());
                }
                Err(_) => {
                    epitaph_sender
                        .take()
                        .and_then(|sender| sender.send(zx::Status::PEER_CLOSED).ok());
                }
                Ok(event) => match event {
                    fcrunner::ComponentControllerEvent::OnPublishDiagnostics {
                        payload, ..
                    } => {
                        diagnostics_sender.take().and_then(|sender| sender.send(payload).ok());
                    }
                },
            }
        }
        epitaph_sender.take().map(|sender| sender.send(zx::Status::PEER_CLOSED).unwrap_or(()));
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl::prelude::*, matches::assert_matches};

    #[fuchsia::test]
    async fn handles_diagnostics_event() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .unwrap();
        let mut controller = ComponentController::new(proxy);
        let receiver = controller.take_diagnostics_receiver();
        assert!(receiver.is_some());
        assert!(controller.take_diagnostics_receiver().is_none());
        stream
            .control_handle()
            .send_on_publish_diagnostics(fdiagnostics::ComponentDiagnostics {
                tasks: Some(fdiagnostics::ComponentTasks::EMPTY),
                ..fdiagnostics::ComponentDiagnostics::EMPTY
            })
            .expect("sent diagnostics");
        assert_matches!(
            receiver.unwrap().await,
            Ok(fdiagnostics::ComponentDiagnostics { tasks: Some(_), .. })
        );
    }

    #[fuchsia::test]
    async fn handles_connection_epitaph() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .unwrap();
        let controller = ComponentController::new(proxy);
        let epitaph_fut = controller.wait_for_epitaph();
        stream.control_handle().shutdown_with_epitaph(zx::Status::UNAVAILABLE);
        assert_eq!(epitaph_fut.await, zx::Status::UNAVAILABLE);
    }

    #[fuchsia::test]
    async fn handles_epitaph_for_closed_connection() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fcrunner::ComponentControllerMarker>()
                .unwrap();
        let controller = ComponentController::new(proxy);
        let epitaph_fut = controller.wait_for_epitaph();
        drop(stream);
        assert_eq!(epitaph_fut.await, zx::Status::PEER_CLOSED);
    }

    #[fuchsia::test]
    async fn handles_epitaph_for_dropped_controller() {
        let (proxy, _) =
            fidl::endpoints::create_proxy::<fcrunner::ComponentControllerMarker>().unwrap();
        let controller = ComponentController::new(proxy);
        let epitaph_fut = controller.wait_for_epitaph();
        drop(controller);
        assert_eq!(epitaph_fut.await, zx::Status::PEER_CLOSED);
    }
}
