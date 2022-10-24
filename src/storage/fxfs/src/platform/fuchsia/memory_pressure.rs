// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::log::*,
    event_listener::{Event, EventListener},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_memorypressure::{
        ProviderMarker, WatcherMarker, WatcherOnLevelChangedResponder, WatcherRequest,
        WatcherRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    futures::{
        stream::{FusedStream, Stream},
        task::{self, Poll},
        FutureExt, StreamExt,
    },
    std::{
        convert::TryFrom,
        pin::Pin,
        sync::{
            atomic::{AtomicU32, Ordering},
            Arc,
        },
    },
};

pub use fidl_fuchsia_memorypressure::Level as MemoryPressureLevel;

/// Helper function to convert a [`WatcherRequest`] to an `OnLevelChanged` request.
fn watcher_request_to_on_level_changed(
    watcher_request: WatcherRequest,
) -> (MemoryPressureLevel, WatcherOnLevelChangedResponder) {
    // Explicitly exhaustive match, so that API changes will break this match.
    match watcher_request {
        WatcherRequest::OnLevelChanged { level, responder } => (level, responder),
    }
}

/// Represents a connection to a system service that provides memory pressure updates.
///
/// Upon dropping, all memory pressure updates will be terminated.
pub struct MemoryPressureMonitor {
    inner: Arc<Inner>,

    /// The handle to the task created by [`Inner::watch_requests`].
    ///
    /// This is held so that dropping this will also drop the task.
    _watch_task_handle: fasync::Task<()>,
}

impl MemoryPressureMonitor {
    /// Connect and start listening to the memory pressure updates from the system service on the
    /// current executor.
    ///
    /// Call `[MemoryPressureMonitor::get_level_stream()`] to get an async [`Stream`] of memory
    /// pressure updates.
    pub async fn start() -> Result<Self, anyhow::Error> {
        debug!("Attempting to connect to fuchsia.memorypressure/Provider");
        let provider = connect_to_protocol::<ProviderMarker>()?;
        debug!("Successfully connected to fuchsia.memorypressure/Provider");

        let (watcher_client, watcher_server) = fidl::endpoints::create_endpoints()?;
        provider.register_watcher(watcher_client)?;

        debug!("Successfully registered as a fuchsia.memorypressure/Watcher");

        Self::try_from(watcher_server)
    }

    /// Returns a [`Stream`] that provides memory pressure updates.
    pub fn get_level_stream(&self) -> MemoryPressureLevelStream {
        MemoryPressureLevelStream::new(self.inner.clone())
    }
}

impl TryFrom<ServerEnd<WatcherMarker>> for MemoryPressureMonitor {
    type Error = anyhow::Error;

    /// Creates an instance of [`MemoryPressureMonitor`] from the server end of a
    /// `fuchsia.memorypressure.Watcher` connection and starts listening on the current executor.
    fn try_from(watcher_server: ServerEnd<WatcherMarker>) -> Result<Self, Self::Error> {
        let watcher_requests = watcher_server.into_stream()?;

        let inner = Inner::new();
        let inner_clone = inner.clone();

        // Spawn a new task to continuously receive memory pressure updates.
        let watch_task_handle = fasync::Task::spawn(inner_clone.watch_requests(watcher_requests));

        Ok(Self { inner, _watch_task_handle: watch_task_handle })
    }
}

struct Inner {
    level: AtomicU32,

    /// An event that is signaled by the [`MemoryPressureMonitor`] when there is a new memory
    /// pressure update.
    event: Event,
}

impl Inner {
    fn new() -> Arc<Self> {
        Arc::new(Self {
            level: AtomicU32::new(MemoryPressureLevel::Normal.into_primitive()),
            event: Event::new(),
        })
    }

    /// A task that continuously listens to requests from `watcher_requests`.
    async fn watch_requests(self: Arc<Self>, mut watcher_requests: WatcherRequestStream) {
        info!("Successfully listening to system memory pressure");

        while !self.process_level_update(watcher_requests.next().await) {}

        debug!("MemoryPressureMonitor::watch_requests is terminating");
    }

    /// Processes a new memory pressure level update.
    ///
    /// Returns `true` if the task should terminate.
    fn process_level_update(
        &self,
        watcher_request: Option<Result<WatcherRequest, fidl::Error>>,
    ) -> bool {
        let watcher_request = match watcher_request {
            Some(v) => v,
            None => {
                info!(
                    "Memory pressure watcher stream has finished. Terminating memory pressure \
                monitoring."
                );
                return true;
            }
        };

        let (level, responder) = match watcher_request {
            Ok(v) => watcher_request_to_on_level_changed(v),
            Err(e) => {
                error!(
                    error = e.as_value(),
                    "FIDL error occurred from memory pressure watcher. Terminating memory pressure \
                    monitoring."
                );
                return true;
            }
        };

        self.update_level(level);

        if let Err(e) = responder.send() {
            error!(
                error = e.as_value(),
                "FIDL error while responding to memory pressure event. Terminating memory pressure \
                monitoring."
            );
            return true;
        }

        false
    }

    /// Gets the current memory pressure level.
    fn level(&self) -> MemoryPressureLevel {
        let raw_level = self.level.load(Ordering::Acquire);
        MemoryPressureLevel::from_primitive(raw_level).expect("Unexpected memory level value")
    }

    /// Updates the current memory pressure level.
    fn update_level(&self, level: MemoryPressureLevel) {
        self.level.store(level.into_primitive(), Ordering::Release);

        // Notify relaxed since reading from `self.level` is appropriately ordered
        self.event.notify_relaxed(usize::MAX);
    }

    fn listen(&self) -> EventListener {
        self.event.listen()
    }
}

pub struct MemoryPressureLevelStream {
    inner: Arc<Inner>,
    prev_level: MemoryPressureLevel,
    listener: Option<EventListener>,
}

impl MemoryPressureLevelStream {
    fn new(inner: Arc<Inner>) -> Self {
        Self { inner, prev_level: MemoryPressureLevel::Normal, listener: None }
    }
}

impl Stream for MemoryPressureLevelStream {
    type Item = MemoryPressureLevel;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> Poll<Option<Self::Item>> {
        loop {
            if let Some(listener) = &mut self.listener {
                futures::ready!(listener.poll_unpin(cx));
            }
            // Immediately reset the listener
            self.listener = Some(self.inner.listen());

            let level = self.inner.level();
            if level != self.prev_level {
                self.prev_level = level;
                return Poll::Ready(Some(level));
            }
        }
    }
}

impl FusedStream for MemoryPressureLevelStream {
    fn is_terminated(&self) -> bool {
        // This never terminates.
        false
    }
}
