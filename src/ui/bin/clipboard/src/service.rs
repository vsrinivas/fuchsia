// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::ClipboardError, inspect, inspect::ServiceInspectData, items::ClipboardItem,
        metadata::ClipboardMetadata, shared::ViewRefPrinter, tasks::LocalTaskTracker,
        watch::WatchServer,
    },
    anyhow::{Context, Error},
    async_utils::hanging_get::client::HangingGetStream,
    fidl::AsHandleRef,
    fidl_fuchsia_ui_clipboard as fclip, fidl_fuchsia_ui_focus as focus,
    fidl_fuchsia_ui_views::ViewRef,
    fuchsia_async::{self as fasync, Task},
    fuchsia_inspect as finspect, fuchsia_zircon as zx,
    futures::{future::Shared, select_biased, FutureExt, StreamExt, TryFutureExt, TryStreamExt},
    std::{
        cell::RefCell,
        collections::HashMap,
        rc::{Rc, Weak},
    },
    tracing::{debug, error, info, warn},
};

/// Tracks all the clipboard-related state associated with a given `ViewRef` (or, to be more
/// precise, the `ViewRef`'s koid).
#[derive(Debug)]
struct ViewRefState {
    /// Whether the `ViewRef`'s koid has been registered to access the clipboard writer protocol.
    registered_for_writer: bool,
    /// Whether the `ViewRef`'s koid has been registered to access the clipboard reader protocol.
    registered_for_reader: bool,
    /// Task that is completed when the `ViewRef` is closed and cleans up the obsolete entry in
    /// [`Service::view_ref_koid_to_view_ref_state`]. The task's future is shared between the reader
    /// and writer, saving redundant kernel calls to `zx_object_wait_async`.
    closed_task: Shared<Task<Result<(), ClipboardError>>>,
}

impl ViewRefState {
    /// Records a change in the `ViewRefState` registration. Returns `true` if mutated successfully,
    /// `false` if previously registered.
    fn register_for(&mut self, register_for: RegisterFor) -> bool {
        match register_for {
            RegisterFor::Writer => {
                if self.registered_for_writer {
                    false
                } else {
                    self.registered_for_writer = true;
                    true
                }
            }
            RegisterFor::Reader => {
                if self.registered_for_reader {
                    false
                } else {
                    self.registered_for_reader = true;
                    true
                }
            }
        }
    }
}

/// Declares the dependency types for [`Service`]. These can be overridden in tests.
pub(crate) trait ServiceDependencies: std::fmt::Debug + 'static {
    type Clock: Clock;
}

/// The current contents of the clipboard.
#[derive(Debug)]
struct ClipboardContents {
    last_modified: zx::Time,
    clipboard_item: Option<ClipboardItem>,
}

impl ClipboardContents {
    fn new(now: zx::Time) -> Self {
        Self { last_modified: now, clipboard_item: None }
    }
}

/// Represents the Clipboard service.
///
/// Instantiate using [`Self::new`].
#[derive(Debug)]
pub(crate) struct Service<T: ServiceDependencies> {
    clock: T::Clock,
    // Each of these fields can be mutated independently on different channels, hence the separate
    // mutable containers.
    /// Tracks the focused leaf view.
    focused_view_ref_koid: RefCell<Option<zx::Koid>>,
    /// The current contents of the clipboard.
    contents: RefCell<ClipboardContents>,
    /// Keeps track of the registration state for each `ViewRef`'s koid.
    view_ref_koid_to_view_ref_state: RefCell<HashMap<zx::Koid, ViewRefState>>,
    /// Implements `Reader.Watch`.
    watch_server: Rc<WatchServer>,

    inspect_data: ServiceInspectData,

    /// Retains pending `Task`s associated with the service. This includes the
    /// * focus watcher task
    /// * WatchServer task
    /// * reader registry and writer registry tasks for each client
    /// * reader and writer tasks for each view
    tracked_tasks: LocalTaskTracker,
}

impl<T: ServiceDependencies> Service<T> {
    /// Creates a new instance of the service and immediately connects to
    /// `fuchsia.ui.focus.FocusChainProvider` to watch for focus changes.
    pub fn new(
        clock: T::Clock,
        focus_provider_proxy: focus::FocusChainProviderProxy,
        inspect_root: &finspect::Node,
    ) -> Rc<Self> {
        let now = clock.now();
        let (watch_server, watch_server_task) =
            WatchServer::new(ClipboardMetadata::with_last_modified(now));
        let inspect_data = ServiceInspectData::new(inspect_root, now);
        let svc = Rc::new(Self {
            clock,
            focused_view_ref_koid: RefCell::new(None),
            contents: RefCell::new(ClipboardContents::new(now)),
            view_ref_koid_to_view_ref_state: RefCell::new(HashMap::new()),
            watch_server,
            inspect_data,
            tracked_tasks: LocalTaskTracker::new(),
        });
        svc.spawn_focus_watcher(focus_provider_proxy);
        svc.tracked_tasks.track(watch_server_task);
        svc
    }

    /// Returns what the `Service` believes is the currently focused view's koid. May return `None`
    /// if the value is currently being mutated.
    ///
    /// For unit tests only.
    #[cfg(test)]
    pub fn read_focused_view_ref_koid(self: &Rc<Self>) -> Option<zx::Koid> {
        self.focused_view_ref_koid.try_borrow().map(|r| *r).unwrap_or(None)
    }

    /// Spawns a singleton task that handles focus change events. Do not call more than once.
    fn spawn_focus_watcher(self: &Rc<Self>, focus_provider_proxy: focus::FocusChainProviderProxy) {
        self.tracked_tasks.track(Task::local(
            Self::focus_watcher_task(Rc::downgrade(self), focus_provider_proxy)
                .unwrap_or_else(|e: Error| error!("Error in focus_watcher_task: {e:?}")),
        ));
    }

    async fn focus_watcher_task(
        weak_this: Weak<Self>,
        focus_provider_proxy: focus::FocusChainProviderProxy,
    ) -> Result<(), Error> {
        let mut stream = HangingGetStream::new(focus_provider_proxy, |ref proxy| {
            proxy.watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
        });

        while let Some(this) = weak_this.upgrade() {
            match stream.next().await {
                Some(Ok(focus_koid_chain)) => {
                    let focused_view_ref_koid: Option<zx::Koid> = focus_koid_chain
                        .focus_chain
                        .as_ref()
                        .and_then(|focus_chain| focus_chain.into_iter().last())
                        .map(|raw| zx::Koid::from_raw(*raw));
                    let old_view_ref_koid =
                        this.focused_view_ref_koid.replace(focused_view_ref_koid);
                    if old_view_ref_koid != focused_view_ref_koid {
                        // Errors logged inside `update_focus`.
                        let _ = this.watch_server.update_focus(focused_view_ref_koid);

                        this.inspect_data
                            .record_event(inspect::EventType::FocusUpdated, this.clock.now());
                        debug!(
                            "Focus changed from {old_view_ref_koid:?} to {focused_view_ref_koid:?}"
                        );
                        if old_view_ref_koid.is_some() != focused_view_ref_koid.is_some() {
                            match &focused_view_ref_koid {
                                Some(_) => {
                                    this.inspect_data.set_healthy();
                                }
                                None => {
                                    this.inspect_data.set_unhealthy("No focused view");
                                }
                            }
                        }
                    }
                }
                Some(Err(service_error)) => {
                    // TODO(fxbug.dev/109359): Make this a recoverable error.
                    this.inspect_data.set_unhealthy("Error in focus watcher stream");
                    error!("Error {service_error:?} in focus watcher stream");
                    break;
                }
                None => {
                    // HangingGetStream is never supposed to return `None`.
                    unreachable!("Unexpected state in focus watch stream");
                }
            }
        }

        Ok(())
    }

    /// Spawns a task that handles a single client's requests to register for write access.
    ///
    /// See also: [`Service::spawn_focused_reader_registry`]
    pub fn spawn_focused_writer_registry(
        self: &Rc<Self>,
        stream: fclip::FocusedWriterRegistryRequestStream,
    ) {
        let task = Task::local(
            Self::focused_writer_registry_task(
                Rc::downgrade(self),
                stream,
                self.inspect_data.scoped_increment_writer_registry_client_count(),
            )
            .unwrap_or_else(|e: Error| error!("Error focused_writer_registry_task: {e:?}")),
        );
        self.tracked_tasks.track(task);
    }

    async fn focused_writer_registry_task(
        weak_this: Weak<Self>,
        mut stream: fclip::FocusedWriterRegistryRequestStream,
        _instance_counter: impl Drop,
    ) -> Result<(), Error> {
        use fclip::FocusedWriterRegistryRequest::*;

        // TODO(fxbug.dev/113422): Switch to let chain when `let_chains` feature is stabilized.
        while let Some(this) = weak_this.upgrade() {
            if let Some(req) = stream
                .try_next()
                .await
                .with_context(|| format!("reading from FocusedWriterRegistryRequestStream"))?
            {
                match req {
                    RequestWriter { payload, responder } => {
                        match this.register_focused_writer_client(payload) {
                            Ok(_) => {
                                responder.send(&mut Ok(()))?;
                            }
                            Err(e) => {
                                let server_error: fclip::ClipboardError = e.into();
                                let mut result = Err(server_error);
                                responder.send(&mut result)?;
                            }
                        };
                    }
                }
            } else {
                break;
            }
        }
        Ok(())
    }

    /// Registers a "focused writer" client (it doesn't have to be focused while registering).
    fn register_focused_writer_client(
        self: &Rc<Self>,
        payload: fclip::FocusedWriterRegistryRequestWriterRequest,
    ) -> Result<(), ClipboardError> {
        let koid = self.validate_and_register_view_ref(payload.view_ref, RegisterFor::Writer)?;
        debug!("Registering for focused writer: koid {koid:?}");
        let writer_request = payload.writer_request.ok_or(ClipboardError::InvalidRequest)?;
        let stream = writer_request.into_stream().map_err(|e| {
            error!("writer_request.into_stream(): {e:?}");
            ClipboardError::internal()
        })?;
        self.spawn_writer(stream, koid);
        Ok(())
    }

    /// Spawns a task that handles the write requests of a single `ViewRef`.
    fn spawn_writer(self: &Rc<Self>, stream: fclip::WriterRequestStream, view_ref_koid: zx::Koid) {
        let task = Task::local(
            Self::writer_task(
                Rc::downgrade(self),
                stream,
                view_ref_koid,
                self.inspect_data.scoped_increment_writer_count(),
            )
            .unwrap_or_else(|e: Error| {
                error!("Error in writer_task: {e:?}");
            }),
        );
        self.tracked_tasks.track(task);
    }

    async fn writer_task(
        weak_this: Weak<Self>,
        mut stream: fclip::WriterRequestStream,
        view_ref_koid: zx::Koid,
        _instance_counter: impl Drop,
    ) -> Result<(), Error> {
        while let Some(this) = weak_this.upgrade() {
            // `select_biased!` is used here to ensure that if the "ViewRef closed" future and a new
            // incoming request are `Ready` simultaneously, the closed future will be handled first.
            select_biased! {
                _ =  Self::wait_for_view_ref_closed(Rc::downgrade(&this), view_ref_koid).fuse() => {
                    info!(
                        "ViewRef {view_ref_koid:?} was closed. Ending its writer_task.",
                    );
                    // Break out of the loop and drop the stream
                    return Ok(());
                },
                req = stream.try_next() => {
                    match req {
                        Ok(Some(req)) => {
                            match req {
                                fclip::WriterRequest::SetItem { payload, responder } => {
                                    let mut result = this
                                        .clone()
                                        .handle_set_item(payload, view_ref_koid)
                                        .map_err(|e| e.into());
                                  debug!("SetItem response: {:?}", &result);
                                  responder.send(&mut result)?;
                                },
                                fclip::WriterRequest::Clear { payload: _, responder } => {
                                    let mut result = this
                                        .clone()
                                        .handle_clear(view_ref_koid)
                                        .map_err(|e| e.into());
                                    responder.send(&mut result)?;
                                },
                            }
                            // Keep looping
                        },
                        Ok(None) => {
                            info!("WriterRequestStream ended for ViewRef {view_ref_koid:?}");
                            return Ok(());
                        },
                        Err(e) => {
                            // Break out of the loop and drop the stream with an error
                            return Err(e.into());
                        },
                    }
                },
            }
        }
        Ok(())
    }

    fn handle_set_item(
        self: &Rc<Self>,
        fidl_item: fclip::ClipboardItem,
        view_ref_koid: zx::Koid,
    ) -> Result<(), ClipboardError> {
        debug!("handle_set_item for ViewRef {view_ref_koid:?}");
        let now = self.clock.now();
        self.ensure_view_is_focused(view_ref_koid, inspect::EventType::WriteAccessDenied, now)?;
        let item = fidl_item.try_into().map_err(|e| {
            self.inspect_data.record_event(inspect::EventType::WriteError, now);
            e
        })?;
        self.inspect_data.record_item(&item, true, now);
        let mut contents = self.contents.borrow_mut();
        contents.clipboard_item.replace(item);
        contents.last_modified = now;
        self.inspect_data.record_event(inspect::EventType::Write, now);
        self.notify_watchers(now);
        Ok(())
    }

    fn handle_clear(self: &Rc<Self>, view_ref_koid: zx::Koid) -> Result<(), ClipboardError> {
        debug!("handle_clear for ViewRef {view_ref_koid:?}");
        let now = self.clock.now();
        self.ensure_view_is_focused(view_ref_koid, inspect::EventType::WriteAccessDenied, now)?;

        let mut contents = self.contents.borrow_mut();
        let _ = contents.clipboard_item.take();
        contents.last_modified = now;
        self.inspect_data.clear_items(now);
        self.inspect_data.record_event(inspect::EventType::Clear, now);
        self.notify_watchers(now);
        Ok(())
    }

    fn notify_watchers(self: &Rc<Self>, last_modified: zx::Time) {
        // Errors logged inside called method.
        let _ = self
            .watch_server
            .update_clipboard_metadata(ClipboardMetadata::with_last_modified(last_modified));
    }

    /// Spawns a task that handles a single client's requests to register for read access.
    ///
    /// See also: [`Service::spawn_focused_writer_registry`].
    pub fn spawn_focused_reader_registry(
        self: &Rc<Self>,
        stream: fclip::FocusedReaderRegistryRequestStream,
    ) {
        let task = Task::local(
            Self::focused_reader_registry_task(
                Rc::downgrade(self),
                stream,
                self.inspect_data.scoped_increment_reader_registry_client_count(),
            )
            .unwrap_or_else(|e: Error| error!("Error in focused_reader_registry_task: {e:?}")),
        );
        self.tracked_tasks.track(task);
    }

    async fn focused_reader_registry_task(
        weak_this: Weak<Self>,
        mut stream: fclip::FocusedReaderRegistryRequestStream,
        _instance_counter: impl Drop,
    ) -> Result<(), Error> {
        use fclip::FocusedReaderRegistryRequest::*;

        // TODO(fxbug.dev/113422): Switch to let chain when `let_chains` feature is stabilized.
        while let Some(this) = weak_this.upgrade() {
            if let Some(req) = stream
                .try_next()
                .await
                .with_context(|| format!("reading from FocusedReaderRegistryRequestStream"))?
            {
                match req {
                    RequestReader { payload, responder } => {
                        match this.register_focused_reader_client(payload) {
                            Ok(_) => {
                                responder.send(&mut Ok(()))?;
                            }
                            Err(e) => {
                                let server_error: fclip::ClipboardError = e.into();
                                let mut result = Err(server_error);
                                responder.send(&mut result)?;
                            }
                        }
                    }
                }
            } else {
                break;
            }
        }
        Ok(())
    }

    /// Registers a "focused reader" client (it doesn't have to be focused while registering).
    fn register_focused_reader_client(
        self: &Rc<Self>,
        payload: fclip::FocusedReaderRegistryRequestReaderRequest,
    ) -> Result<(), ClipboardError> {
        let koid = self.validate_and_register_view_ref(payload.view_ref, RegisterFor::Reader)?;
        debug!("Registering for focused reader: {koid:?}");
        let reader_request = payload.reader_request.ok_or(ClipboardError::InvalidRequest)?;
        let stream = reader_request.into_stream().map_err(|e| {
            error!("reader_request.into_stream(): {e:?}");
            ClipboardError::internal()
        })?;

        self.spawn_reader(stream, koid);

        Ok(())
    }

    fn spawn_reader(self: &Rc<Self>, stream: fclip::ReaderRequestStream, view_ref_koid: zx::Koid) {
        Task::local(
            Self::reader_task(
                Rc::downgrade(self),
                stream,
                view_ref_koid,
                self.inspect_data.scoped_increment_reader_count(),
            )
            .unwrap_or_else(|e: Error| error!("Error in reader_task: {e:?}")),
        )
        .detach();
    }

    async fn reader_task(
        weak_this: Weak<Self>,
        mut stream: fclip::ReaderRequestStream,
        view_ref_koid: zx::Koid,
        _instance_counter: impl Drop,
    ) -> Result<(), Error> {
        while let Some(this) = weak_this.upgrade() {
            // `select_biased!` is used here to ensure that if the "ViewRef closed" future and a new
            // incoming request are `Ready` simultaneously, the closed future will be handled first.
            select_biased! {
                _ = Self::wait_for_view_ref_closed(Rc::downgrade(&this), view_ref_koid).fuse() => {
                    info!("ViewRef {view_ref_koid:?} was closed. Ending its reader_task.");
                    // Break out of the loop and drop the stream
                    return Ok(());
                },
                req = stream.try_next() => {
                    match req {
                        Ok(Some(req)) => {
                            match req {
                                fclip::ReaderRequest::GetItem { payload, responder } => {
                                    let mut result = this
                                        .handle_get_item(payload, view_ref_koid)
                                        .map_err(|e| e.into());
                                    debug!("GetItem response: {:?}", &result);
                                    responder.send(&mut result)?;
                                },
                                fclip::ReaderRequest::Watch { payload: _, responder } => {
                                    // Errors logged and handled inside called method.
                                    let _ = this.watch_server.watch(view_ref_koid, responder);
                                    this.inspect_data.record_event(
                                        inspect::EventType::Watch, this.clock.now());
                                },
                            }
                            // Keep looping
                        },
                        Ok(None) => {
                            info!(
                                "ReaderRequestStream ended for ViewRef {view_ref_koid:?}"
                            );
                            return Ok(());
                        },
                        Err(e) => {
                            // Break out of the loop and drop the stream with an error
                            return Err(e.into());
                        },
                    }
                }
            }
        }
        Ok(())
    }

    fn handle_get_item(
        self: &Rc<Self>,
        _payload: fclip::ReaderGetItemRequest,
        view_ref_koid: zx::Koid,
    ) -> Result<fclip::ClipboardItem, ClipboardError> {
        debug!("get_item for ViewRef {view_ref_koid:?}");
        let now = self.clock.now();
        self.ensure_view_is_focused(view_ref_koid, inspect::EventType::ReadAccessDenied, now)?;
        self.inspect_data.record_event(inspect::EventType::Read, now);
        let item = self.contents.borrow().clipboard_item.as_ref().map(Into::into);
        match item {
            None => Err(ClipboardError::Empty),
            Some(item) => Ok(item),
        }
    }

    /// Validates the given `ViewRef` (including making sure it's not already closed) and retrieves
    /// its koid.
    ///
    /// The checks in this method are loosely based on
    /// //src/ui/scenic/lib/view_tree/view_ref_installed_impl.cc: IsValidViewRef.
    fn ensure_view_ref_is_valid_and_get_koid(
        view_ref: &ViewRef,
    ) -> Result<zx::Koid, ClipboardError> {
        let handle = view_ref.reference.as_handle_ref();
        if handle.is_invalid() {
            info!("Trivially invalid {view_ref:?}");
            return Err(ClipboardError::InvalidViewRef);
        }

        // Check if the `ViewRef` is already closed. We wait for no amount of time
        // ("deadline: infinite past"). If the wait times out and we haven't gotten a signal, that
        // means that the `ViewRef` is still open.
        let view_ref_printer = ViewRefPrinter::from(view_ref);
        match handle.wait_handle(zx::Signals::EVENTPAIR_CLOSED, zx::Time::INFINITE_PAST) {
            Ok(_) => {
                warn!("{view_ref:?} was already closed", view_ref = &view_ref_printer);
                return Err(ClipboardError::InvalidViewRef);
            }
            Err(status) => {
                if status != zx::Status::TIMED_OUT {
                    error!(
                        "Couldn't get signals for {view_ref:?}: {status:?}",
                        view_ref = &view_ref_printer
                    );
                    return Err(ClipboardError::internal_from_status(status));
                }
            }
        };

        let basic_info = handle.basic_info().map_err(|status| {
            info!("Error {status} while getting basic info for ViewRef {view_ref:?}");
            ClipboardError::InvalidViewRef
        })?;
        if !basic_info.rights.contains(zx::Rights::WAIT) {
            info!("Bad rights in ViewRef: {basic_info:?}");
            return Err(ClipboardError::InvalidViewRef);
        }
        Ok(basic_info.koid)
    }

    fn ensure_view_is_focused(
        self: &Rc<Self>,
        view_ref_koid: zx::Koid,
        error_event_type: inspect::EventType,
        now: zx::Time,
    ) -> Result<(), ClipboardError> {
        match self.focused_view_ref_koid.borrow().as_ref() {
            None => {
                warn!(
                    "Asserted ViewRef {view_ref_koid:?} was focused before a focus chain was set",
                );
                self.inspect_data.record_event(error_event_type, now);
                Err(ClipboardError::internal_from_status(zx::Status::UNAVAILABLE))
            }
            Some(focused_view_ref_koid) => {
                if *focused_view_ref_koid == view_ref_koid {
                    Ok(())
                } else {
                    info!("ViewRef {view_ref_koid:?} is not focused");
                    self.inspect_data.record_event(error_event_type, now);
                    Err(ClipboardError::Unauthorized)
                }
            }
        }
    }

    /// 1. Checks whether the given `ViewRef` is valid and still open.
    /// 2. Registers to be notified when it's closed.
    /// 3. Records the registration (for either writes or reads) and blocks duplicate registrations
    ///    for the same `ViewRef` koid.
    fn validate_and_register_view_ref(
        self: &Rc<Self>,
        view_ref: impl Into<Option<ViewRef>>,
        register_for: RegisterFor,
    ) -> Result<zx::Koid, ClipboardError> {
        let view_ref = view_ref.into();
        let view_ref = view_ref.ok_or(ClipboardError::InvalidRequest)?;

        let koid = Self::ensure_view_ref_is_valid_and_get_koid(&view_ref)?;

        let mut view_ref_koid_to_view_ref_state = self.view_ref_koid_to_view_ref_state.borrow_mut();

        let view_ref_state =
            view_ref_koid_to_view_ref_state.entry(koid).or_insert_with_key(|koid| {
                let closed_task = self.make_view_ref_closed_task(view_ref, *koid);

                ViewRefState {
                    registered_for_writer: false,
                    registered_for_reader: false,
                    closed_task,
                }
            });

        if !view_ref_state.register_for(register_for) {
            warn!("ViewRef {koid:?} already registered for focused {register_for}");
            return Err(ClipboardError::DuplicateViewRef);
        }

        Ok(koid)
    }

    fn make_view_ref_closed_task(
        self: &Rc<Self>,
        view_ref: ViewRef,
        view_ref_koid: zx::Koid,
    ) -> Shared<Task<Result<(), ClipboardError>>> {
        let weak_this = Rc::downgrade(self);
        let task = Task::local(async move {
            let handle = view_ref.reference.as_handle_ref();
            fasync::OnSignals::new(&handle, zx::Signals::EVENTPAIR_CLOSED).await.map_err(
                move |status| {
                    error!("OnSignals for {view_ref_koid:?}: {status:?}");
                    ClipboardError::internal_from_status(status)
                },
            )?;
            debug!("{view_ref_koid:?} closed");

            if let Some(this) = weak_this.upgrade() {
                match this.view_ref_koid_to_view_ref_state.borrow_mut().remove(&view_ref_koid) {
                    None => {
                        error!("ViewRef {view_ref_koid:?} was already removed from the registry");
                    }
                    Some(_) => {
                        debug!(
                            "Successfully removed closed ViewRef \
                            {view_ref_koid:?} from registry"
                        );
                    }
                }
                // Errors logged inside called method.
                let _ = this.watch_server.forget_view_ref(view_ref_koid);
            } else {
                debug!("Service was already shut down when ViewRef {view_ref_koid:?} was closed");
            }
            Ok(())
        });
        task.shared()
    }

    /// The given `ViewRef`'s koid must already be registered.
    async fn wait_for_view_ref_closed(
        weak_this: Weak<Self>,
        view_ref_koid: zx::Koid,
    ) -> Result<(), ClipboardError> {
        let closed_fut = weak_this
            .upgrade()
            .or_else(|| {
                debug!("Tried to wait for ViewRef {view_ref_koid:?} close after service dropped");
                None
            })
            .and_then(|this| {
                this.view_ref_koid_to_view_ref_state
                    .borrow()
                    .get(&view_ref_koid)
                    .map(|view_ref_state| view_ref_state.closed_task.clone())
            })
            .or_else(|| {
                error!("Tried to wait for unregistered ViewRef {view_ref_koid:?} to be closed");
                None
            })
            .ok_or_else(|| ClipboardError::internal())?;

        closed_fut.await
    }
}

#[derive(Debug, Copy, Clone)]
enum RegisterFor {
    Writer,
    Reader,
}

impl std::fmt::Display for RegisterFor {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            RegisterFor::Writer => "writer",
            RegisterFor::Reader => "reader",
        })
    }
}

/// Generic clock trait.
///
/// All clones of a given original `Clock` instance must represent the same underlying source of
/// time values. In other words:
///
/// ```ignore
/// let clock_a = MySettableClock::new();
/// let clock_b = clock_a.clone();
/// let clock_c = MySettableClock::new();
///
/// clock_a.set(zx::Time::from_nanos(17);
/// clock_c.set(zx::Time::from_nanos(29));
/// assert_eq!(clock_a.now(), zx::Time::from_nanos(17));
/// assert_eq!(clock_b.now(), zx::Time::from_nanos(17));
/// assert_eq!(clock_c.now(), zx::Time::from_nanos(29));
///
/// clock_b.set(zx::Time::from_nanos(34));
/// assert_eq!(clock_a.now(), zx::Time::from_nanos(34));
/// assert_eq!(clock_b.now(), zx::Time::from_nanos(34));
/// assert_eq!(clock_c.now(), zx::Time::from_nanos(29));
/// ```
pub(crate) trait Clock: Clone + std::fmt::Debug {
    fn now(&self) -> zx::Time;
}

/// Production version of the clock.
#[derive(Debug, Default, Clone)]
pub(crate) struct MonotonicClock;

impl MonotonicClock {
    pub fn new() -> Self {
        Self::default()
    }
}

impl Clock for MonotonicClock {
    fn now(&self) -> zx::Time {
        zx::Time::get_monotonic()
    }
}

// Note: Unit tests can be found in service_tests.rs.
