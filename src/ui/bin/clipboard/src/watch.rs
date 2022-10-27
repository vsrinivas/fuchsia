// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides a hanging-get implementation of `fuchsia.ui.clipboard/Reader.Watch`.
//!
//! Unit tests are in [`crate::watch_tests`].

#![allow(unused_imports, dead_code)] // TODO(fxbug.dev/110935)

use {
    crate::{errors::ClipboardError, metadata::ClipboardMetadata},
    derivative::Derivative,
    fidl::endpoints::{ControlHandle, Responder},
    fidl_fuchsia_ui_clipboard::{self as fclip, ReaderWatchResponder},
    fuchsia_async::Task,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, StreamExt},
    std::{
        cell::{RefCell, RefMut},
        collections::HashMap,
        rc::{Rc, Weak},
    },
    tracing::*,
};

#[derive(Derivative)]
#[derivative(Debug)]
enum WatchingState {
    /// No client is currently connected and watching the clipboard state on behalf of the client's
    /// View.
    NotWatching,
    /// A client is currently connected and watching the clipboard state on behalf of the client's
    /// View.
    Watching(#[derivative(Debug = "ignore")] ReaderWatchResponder),
}

impl WatchingState {
    pub fn is_watching(&self) -> bool {
        match self {
            WatchingState::NotWatching => false,
            WatchingState::Watching(_) => true,
        }
    }

    /// If the state is `Watching`, changes it to `NotWatching` and returns the responder.
    pub fn take_responder(&mut self) -> Option<ReaderWatchResponder> {
        match self {
            WatchingState::NotWatching => None,
            WatchingState::Watching(_) => {
                let output = std::mem::take(self);
                output.into_responder()
            }
        }
    }

    /// Consumes the state and returns a `ReaderWatchResponder` if the state contained one.
    pub fn into_responder(self) -> Option<ReaderWatchResponder> {
        match self {
            WatchingState::NotWatching => None,
            WatchingState::Watching(responder) => Some(responder),
        }
    }
}

impl Default for WatchingState {
    fn default() -> Self {
        Self::NotWatching
    }
}

#[derive(Debug)]
struct ViewRefState {
    /// Whether there's currently a [`ReaderWatchResponder`] waiting for a reply.
    watching: WatchingState,
    /// Whether the `ClipboardMetadata` has been modified since the last time an update was sent
    /// for this ViewRef.
    dirty: bool,
}

impl ViewRefState {
    fn is_watching(&self) -> bool {
        self.watching.is_watching()
    }

    fn is_dirty(&self) -> bool {
        self.dirty
    }
}

impl Default for ViewRefState {
    fn default() -> Self {
        Self { watching: WatchingState::NotWatching, dirty: true }
    }
}

/// Represents a pending request in [`WatchServer`]'s internal queue.
#[derive(Derivative)]
#[derivative(Debug)]
enum Message {
    /// See [`Server::watch`].
    Watch {
        view_ref_koid: zx::Koid,
        #[derivative(Debug = "ignore")]
        responder: ReaderWatchResponder,
    },

    /// See [`Server::update_clipboard_metadata`].
    UpdateClipboardMetadata { clipboard_metadata: ClipboardMetadata },

    /// See [`Server::update_focus`].
    UpdateFocus { focused_view_ref_koid: Option<zx::Koid> },

    /// See [`Server::forget_view_ref`].
    ForgetViewRef { view_ref_koid: zx::Koid },

    /// See [`Server::notify`].
    Notify { force_notify_view_ref_koid: Option<zx::Koid> },
}

impl Message {
    /// Consumes the message and returns its responder, if any.
    fn into_responder(self) -> Option<ReaderWatchResponder> {
        match self {
            Message::Watch { view_ref_koid: _, responder } => Some(responder),
            _ => None,
        }
    }
}

/// A helper for handling calls to `fuchsia.ui.clipboard/Reader.Watch`.
///
/// This helper adheres to the [hanging-get][hanging-get] pattern but does not use the
/// [`async_utils::hanging_get`] library because the library proved to be impractical for the
/// specialized requirement of only notifying new or currently focused clients (see
/// [`WatchServer::watch`]).
///
/// # Usage
///
/// -   Use [`WatchServer::new`] to create an instance of the server and spawn a new local task in
///     which it runs.
/// -   When a `Reader` client sends a `Watch` request, call [`WatchServer::watch`].
/// -   When the `ClipboardMetadata` changes, call [`WatchServer::update_clipboard_metadata`].
/// -   When the focused view changes, call [`WatchServer::update_focus`].
/// -   When a `ViewRef` becomes invalid, call [`WatchServer::forget_view_ref`].
///
/// Note that the `WatchServer` does not do all bookkeeping on its own.
///
/// In particular, it expects its owner (the [`crate::service::ClipboardService`]) to track the
/// association between a `Reader` and the ViewRef koid that the `Reader` registered,
/// and thus to detect if multiple `Reader`s are attempting to access the clipboard on behalf of a
/// single ViewRef koid.
///
/// -   The _clipboard service_ detects duplicate registrations for a single ViewRef koid, and
///     returns a [`fidl_fuchsia_ui_clipboard::ClipboardError::InvalidViewRef`].
/// -   The _watch server_ detects multiple concurrent `Watch` calls for a single ViewRef and closes
///     the offending channel with a `ZX_ERR_BAD_STATE` epitaph.
///
/// [hanging-get]: https://fuchsia.dev/fuchsia-src/development/api/fidl#hanging-get
#[derive(Debug)]
pub(crate) struct WatchServer {
    inner: RefCell<Inner>,
    tx: mpsc::UnboundedSender<Message>,
}

#[derive(Debug)]
struct Inner {
    clipboard_metadata: ClipboardMetadata,
    focused_view_ref_koid: Option<zx::Koid>,
    view_ref_koid_to_state: HashMap<zx::Koid, ViewRefState>,
}

impl WatchServer {
    /// Creates a new instance and spawns a new local [`Task`] in which the server runs.
    ///
    /// Example:
    /// ```
    /// let (server, _task) = WatchServer::new(/* ... */)
    /// ```
    ///
    /// NOTE: Remember that `let _ = ...` means drop immediately.
    #[must_use = "The Task must be retained or `.detach()`ed."]
    pub fn new(initial_state: ClipboardMetadata) -> (Rc<Self>, Task<()>) {
        let (tx, mut rx) = mpsc::unbounded::<Message>();
        let server = Rc::new(WatchServer {
            inner: RefCell::new(Inner {
                clipboard_metadata: initial_state,
                focused_view_ref_koid: None,
                view_ref_koid_to_state: HashMap::new(),
            }),
            tx,
        });
        let weak_server = server.weak();
        let server_task = Task::local(async move {
            debug!("[WatchServer::new] Server task started");
            // TODO: Switch to let chain when `let_chains` feature is stabilized.
            while let Some(server) = weak_server.upgrade() {
                while let Some(message) = rx.next().await {
                    server.process_message(message);
                }
            }
            debug!("[WatchServer::new] Server task ended");
        });
        (server, server_task)
    }

    fn process_message(self: &Rc<Self>, message: Message) {
        use Message::*;

        debug!("[process_message] {:?}", &message);

        let inner = self.inner.borrow_mut();

        match message {
            Watch { view_ref_koid, responder } => {
                self.internal_watch(inner, view_ref_koid, responder);
            }
            UpdateClipboardMetadata { clipboard_metadata } => {
                self.internal_update_clipboard_metadata(inner, clipboard_metadata);
            }
            UpdateFocus { focused_view_ref_koid } => {
                self.internal_update_focus(inner, focused_view_ref_koid);
            }
            ForgetViewRef { view_ref_koid } => {
                self.internal_forget_view_ref(inner, view_ref_koid);
            }
            Notify { force_notify_view_ref_koid: force_view_ref_koid } => {
                self.internal_notify(inner, force_view_ref_koid);
            }
        };
    }

    /// Attempts to enqueue the given message. If this fails, sends an error through the
    /// `responder`, if the message contains one.
    fn enqueue_message(self: &Rc<Self>, message: Message) -> Result<(), ClipboardError> {
        debug!("[enqueue_message] {:?}", &message);
        self.tx.unbounded_send(message).map_err(|try_send_error| {
            let err_str = try_send_error.to_string();
            let unsent_message = try_send_error.into_inner();
            warn!("[enqueue_message] Couldn't enqueue {:?}: {}", unsent_message, err_str);
            if let Some(responder) = unsent_message.into_responder() {
                let _ =
                    responder.send(&mut Err(fclip::ClipboardError::Internal)).map_err(|fidl_err| {
                        warn!(?fidl_err, "[enqueue_message] Couldn't send error response");
                    });
            }
            ClipboardError::Internal(zx::Status::UNAVAILABLE)
        })
    }

    /// Enqueues a request for the given `responder` to watch the `ClipboardMetadata` for changes,
    /// following a "hanging-get" pattern.
    ///
    /// 1.  If this is the first `Watch` request for the given `view_ref_koid`:
    ///
    ///     *   If the view is focused, a successful response with the `ClipboardMetadata` will be
    ///         sent.
    ///
    ///     *   If the view is _not_ focused, `ClipboardError.UNAUTHORIZED` will be sent.
    ///
    /// 2.  If this is a subsequent `Watch` request for the given `view_ref_koid`:
    ///
    ///     *   The server will wait until the view is focused _and_ there has been a change, before
    ///         sending a successful `ClipboardMetadata` response.
    ///
    ///     *   If an additional `Watch` request for the same `view_ref_koid` is received before the
    ///         previous one was answered, the server will close the `responder`'s channel with an
    ///         epitaph of `ZX_ERR_BAD_STATE`.
    pub fn watch(
        self: &Rc<Self>,
        view_ref_koid: zx::Koid,
        responder: ReaderWatchResponder,
    ) -> Result<(), ClipboardError> {
        self.enqueue_message(Message::Watch { view_ref_koid, responder })
    }

    fn internal_watch(
        self: &Rc<Self>,
        mut inner: RefMut<'_, Inner>,
        view_ref_koid: zx::Koid,
        responder: ReaderWatchResponder,
    ) {
        let force_notify_view_ref_koid =
            if !inner.view_ref_koid_to_state.contains_key(&view_ref_koid) {
                inner.view_ref_koid_to_state.insert(
                    view_ref_koid,
                    ViewRefState { watching: WatchingState::Watching(responder), dirty: true },
                );
                Some(view_ref_koid)
            } else {
                let mut view_ref_state =
                    inner.view_ref_koid_to_state.get_mut(&view_ref_koid).unwrap();
                if let WatchingState::Watching(old_responder) = &view_ref_state.watching {
                    warn!(
                        ?view_ref_koid,
                        old_responder = ?old_responder.control_handle(),
                        new_responder = ?responder.control_handle(),
                        "[internal_watch] Attempted multiple concurrent Watch requests. \
                    Closing channel."
                    );
                    responder.control_handle().shutdown_with_epitaph(zx::Status::BAD_STATE);
                    // If the clipboard service is doing its job, the old and new responder are
                    // supposed to represent the same channel, so shutting down the old one should
                    // be a no-op.
                    old_responder.control_handle().shutdown_with_epitaph(zx::Status::BAD_STATE);
                    // Ensure that some future, better-behaved client will still be able to watch
                    // the clipboard on behalf of this ViewRef.
                    // Note that we're not calling `forget_view_ref()` here, because the ViewRef is
                    // still valid; we're just dropping its previous client.
                    inner.view_ref_koid_to_state.remove(&view_ref_koid);
                    return;
                } else {
                    view_ref_state.watching = WatchingState::Watching(responder);
                }
                None
            };
        let _ = match force_notify_view_ref_koid {
            Some(koid) => self.force_notify(koid),
            None => self.notify_eligible(),
        };
    }

    /// Enqueues a request for the eligible (focused) view, if any, to be notified.
    fn notify_eligible(self: &Rc<Self>) -> Result<(), ClipboardError> {
        self.enqueue_message(Message::Notify { force_notify_view_ref_koid: None })
    }

    /// Enqueues a request to notify the specified view regardless of whether it's focused. (This is
    /// used for first-time watch calls.)
    fn force_notify(
        self: &Rc<Self>,
        force_notify_view_ref_koid: zx::Koid,
    ) -> Result<(), ClipboardError> {
        self.enqueue_message(Message::Notify {
            force_notify_view_ref_koid: Some(force_notify_view_ref_koid),
        })
    }

    #[allow(unused_variables)]
    fn internal_notify(
        self: &Rc<Self>,
        inner: RefMut<'_, Inner>,
        force_notify_view_ref_koid: Option<zx::Koid>,
    ) {
        // TODO(fxbug.dev/110935)
        todo!()
    }

    /// Enqueues an update of the clipboard metadata, i.e. the state being watched.
    ///
    /// If the metadata has changed, this will mark all current watchers as dirty, and will enqueue
    /// a request for eligible watchers to be notified.
    ///
    /// Note that updating the metadata does not guarantee that the currently focused observer will
    /// receive that exact snapshot. The server will always send the most recent snapshot that it is
    /// aware of when it gets to sending updates, so if `update_clipboard_metadata` is called many
    /// times in quick succession, only the latest snapshot might be sent.
    pub fn update_clipboard_metadata(
        self: &Rc<Self>,
        clipboard_metadata: ClipboardMetadata,
    ) -> Result<(), ClipboardError> {
        self.enqueue_message(Message::UpdateClipboardMetadata { clipboard_metadata })
    }

    fn internal_update_clipboard_metadata(
        self: &Rc<Self>,
        mut inner: RefMut<'_, Inner>,
        clipboard_metadata: ClipboardMetadata,
    ) {
        if inner.clipboard_metadata != clipboard_metadata {
            debug!(
                old_metadata = ?inner.clipboard_metadata,
                new_metadata = ?clipboard_metadata,
                "[internal_update_clipboard_metadata] changed"
            );
            inner.clipboard_metadata = clipboard_metadata;
            inner.view_ref_koid_to_state.values_mut().for_each(|view_ref_state| {
                view_ref_state.dirty = true;
            });
            // `enqueue_message` already logs errors.
            let _ = self.notify_eligible();
        } else {
            debug!(
                new_metadata = ?clipboard_metadata,
                "[internal_update_clipboard_metadata] unchanged"
            );
        }
    }

    /// Enqueues an update of the currently focused ViewRef.
    ///
    /// If the focused ViewRef changes, this will enqueue a request for eligible watchers to be
    /// notified.
    ///
    /// Note that updating the focus to a particular koid does not guarantee that that koid's
    /// watcher will receive an update. The server will always send updates to the most recently
    /// focused observer that it is aware of, so if `update_focus` is called many times in quick
    /// succession, only the latest koid's observer will actually receive an update.
    pub fn update_focus(
        self: &Rc<Self>,
        focused_view_ref_koid: impl Into<Option<zx::Koid>>,
    ) -> Result<(), ClipboardError> {
        self.enqueue_message(Message::UpdateFocus {
            focused_view_ref_koid: focused_view_ref_koid.into(),
        })
    }

    fn internal_update_focus(
        self: &Rc<Self>,
        mut inner: RefMut<'_, Inner>,
        focused_view_ref_koid: Option<zx::Koid>,
    ) {
        debug!(
            "[internal_update_focus] from {:?} to {:?}",
            inner.focused_view_ref_koid, focused_view_ref_koid
        );
        if inner.focused_view_ref_koid != focused_view_ref_koid {
            inner.focused_view_ref_koid = focused_view_ref_koid;
            let _ = self.notify_eligible();
        }
    }

    /// Enqueues a request to forget about a given ViewRef. This method should be called when a
    /// ViewRef is closed or becomes invalid.
    pub fn forget_view_ref(self: &Rc<Self>, view_ref_koid: zx::Koid) -> Result<(), ClipboardError> {
        self.enqueue_message(Message::ForgetViewRef { view_ref_koid })
    }

    fn internal_forget_view_ref(
        self: &Rc<Self>,
        mut inner: RefMut<'_, Inner>,
        view_ref_koid: zx::Koid,
    ) {
        inner.view_ref_koid_to_state.remove(&view_ref_koid);
        if let Some(_view_ref_koid) = inner.focused_view_ref_koid {
            inner.focused_view_ref_koid = None;
        }
    }

    /// Returns a `Weak` reference to itself.
    pub fn weak(self: &Rc<Self>) -> Weak<Self> {
        Rc::downgrade(self)
    }

    fn log_fidl_send_error(fidl_err: fidl::Error, view_ref_koid: zx::Koid) {
        error!(?fidl_err, ?view_ref_koid, "failed to send response");
    }
}

// Note: Unit tests can be found in watch_tests.rs.
