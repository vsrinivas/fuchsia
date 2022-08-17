// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        log::*,
        platform::fuchsia::{file::FxFile, node::FxNode},
    },
    anyhow::{Context, Error},
    async_utils::event::{Event, EventWaitResult},
    fuchsia_async as fasync,
    fuchsia_zircon::{
        self as zx,
        sys::{zx_page_request_command_t::ZX_PAGER_VMO_READ, zx_system_get_num_cpus},
        AsHandleRef, PacketContents, PagerPacket, SignalPacket,
    },
    futures::channel::oneshot,
    once_cell::sync::OnceCell,
    std::{
        collections::{hash_map::Entry, HashMap},
        ops::Range,
        sync::{Arc, Mutex, Weak},
        thread::JoinHandle,
    },
};

/// A multi-threaded Fuchsia async executor for handling pager requests coming from the kernel. This
/// is separate from the primary executor. All pager requests must be handled on this executor, so
/// that re-entrant calls to the kernel cannot deadlock the threads in the primary executor.
///
/// This executor can be safely shared across multiple [`Pager`]s.
pub struct PagerExecutor {
    /// Join handle to the main thread that starts and runs the executor.
    primary_thread_handle: Option<JoinHandle<()>>,

    /// A handle to the async executor.
    executor_handle: fasync::EHandle,

    /// An event to signal when the executor should terminate.
    terminate_event: Event,
}

/// A thread owned by [`PagerExecutor`] dedicated to pulling packets out of a port.
///
/// If [`PortThread::terminate()`] is not called, dropping this struct will join the thread.
struct PortThread {
    /// The handle to join the thread.
    join_handle: Mutex<Option<JoinHandle<()>>>,

    /// The port on which the thread is polling.
    port: Arc<zx::Port>,
}

pub struct Pager {
    pager: zx::Pager,
    inner: Arc<Mutex<Inner>>,
    port_thread: PortThread,
}

#[derive(Default)]
struct Inner {
    files: HashMap<u64, FileHolder>,
}

// FileHolder is used to retain either a strong or a weak reference to a file.  If there are any
// child VMOs that have been shared, then we will have a strong reference which is required to keep
// the file alive.  When we detect that there are no more children, we can downgrade to a weak
// reference which will allow the file to be cleaned up if there are no other uses.
enum FileHolder {
    Strong(Arc<FxFile>),
    Weak(Weak<FxFile>),
}

fn watch_for_zero_children(port: &zx::Port, file: &FxFile) -> Result<(), zx::Status> {
    file.vmo().as_handle_ref().wait_async_handle(
        port,
        file.object_id(),
        zx::Signals::VMO_ZERO_CHILDREN,
        zx::WaitAsyncOpts::empty(),
    )
}

impl From<Arc<FxFile>> for FileHolder {
    fn from(file: Arc<FxFile>) -> FileHolder {
        FileHolder::Strong(file)
    }
}

impl From<Weak<FxFile>> for FileHolder {
    fn from(file: Weak<FxFile>) -> FileHolder {
        FileHolder::Weak(file)
    }
}

impl Drop for PagerExecutor {
    fn drop(&mut self) {
        self.terminate_event.signal();

        if let Some(handle) = self.primary_thread_handle.take() {
            handle
                .join()
                .unwrap_or_else(|_| error!("Error occurred joining primary pager executor thread"));
        }
    }
}

impl PagerExecutor {
    pub fn global_instance() -> Arc<Self> {
        static INSTANCE: OnceCell<Arc<PagerExecutor>> = OnceCell::new();
        INSTANCE
            .get_or_init(|| Arc::new(futures::executor::block_on(PagerExecutor::start()).unwrap()))
            .clone()
    }

    pub async fn start() -> Result<Self, Error> {
        let (ehandle_tx, ehandle_rx) = oneshot::channel();

        let terminate_event = Event::new();
        let terminate_or_dropped_event = terminate_event.wait_or_dropped();

        let primary_thread_handle = std::thread::spawn(move || {
            let mut executor = fasync::SendExecutor::new(Self::get_num_threads())
                .expect("Failed to create executor for PagerExecutor");
            executor.run(PagerExecutor::executor_worker_lifecycle(
                ehandle_tx,
                terminate_or_dropped_event,
            ));
        });

        let executor_handle =
            ehandle_rx.await.context("Failed to setup newly created PagerExecutor")?;

        Ok(Self {
            primary_thread_handle: Some(primary_thread_handle),
            executor_handle,
            terminate_event,
        })
    }

    /// Gets the number of threads to run the executor with.
    fn get_num_threads() -> usize {
        let num_cpus = unsafe { zx_system_get_num_cpus() };

        std::cmp::max(num_cpus, 1) as usize
    }

    async fn executor_worker_lifecycle(
        ehandle_tx: oneshot::Sender<fasync::EHandle>,
        terminate_wait: EventWaitResult,
    ) {
        let executor_handle = fasync::EHandle::local();

        // Reply to creator with executor handle, ignoring errors.
        ehandle_tx.send(executor_handle.clone()).unwrap_or(());

        debug!("Pager executor started successfully");

        // Keep executor alive until termination is signalled or the event is dropped.
        terminate_wait.await.unwrap_or_default();

        debug!("Pager executor received terminate signal and will terminate");
    }
}

impl PortThread {
    fn start(executor: Arc<PagerExecutor>, inner: Arc<Mutex<Inner>>) -> Result<Self, Error> {
        let port = Arc::new(zx::Port::create()?);
        let port_clone = port.clone();

        let join_handle =
            std::thread::spawn(move || Self::thread_lifecycle(executor, port_clone, inner));

        Ok(Self { join_handle: Mutex::new(Some(join_handle)), port })
    }

    fn port(&self) -> &zx::Port {
        &self.port
    }

    fn thread_lifecycle(
        executor: Arc<PagerExecutor>,
        port: Arc<zx::Port>,
        inner: Arc<Mutex<Inner>>,
    ) {
        debug!("Pager port thread started successfully");

        loop {
            match port.wait(zx::Time::INFINITE) {
                Ok(packet) => {
                    match packet.contents() {
                        PacketContents::Pager(contents) => {
                            Self::receive_pager_packet(
                                packet.key(),
                                contents,
                                &executor.executor_handle,
                                inner.clone(),
                            );
                        }
                        PacketContents::SignalOne(signals) => {
                            Self::receive_signal_packet(
                                packet.key(),
                                signals,
                                inner.clone(),
                                port.clone(),
                            );
                        }
                        PacketContents::User(_) => {
                            debug!("Pager port thread received signal to terminate");
                            break;
                        }
                        _ => unreachable!(), // We don't expect any other kinds of packets
                    }
                }
                Err(e) => error!(error = e.as_value(), "Port::wait failed"),
            }
        }
    }

    fn receive_pager_packet(
        key: u64,
        contents: PagerPacket,
        executor_handle: &fasync::EHandle,
        inner: Arc<Mutex<Inner>>,
    ) {
        if contents.command() != ZX_PAGER_VMO_READ {
            return;
        }

        // Spawn task on the executor so we don't block the pager thread.
        fasync::Task::spawn_on(executor_handle, async move {
            let file = {
                let inner = inner.lock().unwrap();
                match inner.files.get(&key) {
                    Some(FileHolder::Strong(file)) => file.clone(),
                    Some(FileHolder::Weak(file)) => {
                        if let Some(file) = file.upgrade() {
                            file
                        } else {
                            return;
                        }
                    }
                    _ => {
                        return;
                    }
                }
            };
            file.page_in(contents.range()).await;
        })
        .detach();
    }

    fn receive_signal_packet(
        key: u64,
        signals: SignalPacket,
        inner: Arc<Mutex<Inner>>,
        port: Arc<zx::Port>,
    ) {
        assert!(signals.observed().contains(zx::Signals::VMO_ZERO_CHILDREN));

        // To workaround races, we must check to see if the vmo really does have no
        // children.
        let _strong;
        let mut inner = inner.lock().unwrap();
        if let Some(holder) = inner.files.get_mut(&key) {
            if let FileHolder::Strong(file) = holder {
                match file.vmo().info() {
                    Ok(info) => {
                        if info.num_children == 0 {
                            file.on_zero_children();
                            // Downgrade to a weak reference. Keep a strong reference until
                            // we drop the lock because otherwise there's the potential to
                            // deadlock (when the file is dropped, it will call
                            // unregister_file which needs to take the lock).
                            let weak = Arc::downgrade(file);
                            _strong = std::mem::replace(holder, FileHolder::Weak(weak));
                        } else {
                            // There's not much we can do here if this fails, so we panic.
                            watch_for_zero_children(&port, file).unwrap();
                        }
                    }
                    Err(e) => {
                        error!(error = e.as_value(), "Vmo::info failed");
                    }
                }
            }
        }
    }

    fn terminate(&self) {
        // Queue a packet on the port to notify the thread to terminate.
        self.port
            .queue(&zx::Packet::from_user_packet(0, 0, zx::UserPacket::from_u8_array([0; 32])))
            .unwrap();

        if let Some(join_handle) = self.join_handle.lock().unwrap().take() {
            join_handle.join().unwrap();
        }
    }
}

impl Drop for PortThread {
    fn drop(&mut self) {
        self.terminate();
    }
}

/// Pager handles page requests. It is a per-volume object.
impl Pager {
    pub fn new(executor: Arc<PagerExecutor>) -> Result<Self, Error> {
        let pager = zx::Pager::create(zx::PagerOptions::empty())?;
        let inner = Arc::new(Mutex::new(Inner::default()));
        let port_thread = PortThread::start(executor, inner.clone())?;

        Ok(Pager { pager, inner, port_thread })
    }

    /// Creates a new VMO to be used with the pager. Page requests will not be serviced until
    /// [`Pager::register_file()`] is called.
    pub fn create_vmo(&self, pager_key: u64, initial_size: u64) -> Result<zx::Vmo, Error> {
        Ok(self.pager.create_vmo(
            zx::VmoOptions::RESIZABLE,
            self.port_thread.port(),
            pager_key,
            initial_size,
        )?)
    }

    /// Registers a file with the pager. Page requests are not properly serviced until
    /// start_servicing is called. Any requests that arrive prior to that will be fulfilled with
    /// zero pages.
    pub fn register_file(&self, file: &Arc<FxFile>) -> u64 {
        let file_key = file.object_id();

        self.inner.lock().unwrap().files.insert(file_key, FileHolder::Weak(Arc::downgrade(file)));

        file_key
    }

    /// Unregisters a file with the pager.
    pub fn unregister_file(&self, file: &FxFile) {
        let file_key = file.object_id();

        let mut inner = self.inner.lock().unwrap();
        if let Entry::Occupied(o) = inner.files.entry(file_key) {
            // Note that this function is currently only called by the destructor of [`FxFile`].
            // If anything else calls this function, the [`FileHolder`] must be dropped outside the
            // lock to prevent deadlocking, since [`FxFile::drop()`] attempts to call this function.
            o.remove();
        }
    }

    /// Starts servicing page requests for the given object. Returns false if the file is already
    /// being serviced. When there are no more references, [`FxFile::on_zero_children`] will be
    /// called.
    pub fn start_servicing(&self, file: &FxFile) -> Result<bool, Error> {
        let file_key = file.object_id();
        let mut inner = self.inner.lock().unwrap();
        let file = inner.files.get_mut(&file_key).unwrap();

        if let FileHolder::Weak(weak) = file {
            // Should never fail because start_servicing should be called by FxFile.
            let strong = weak.upgrade().unwrap();

            // Watching for zero children isn't required to be done on the pager executor but it can
            // be cleanly and efficiently (memory and thread usage) muxed onto it, so we do so here.
            watch_for_zero_children(self.port_thread.port(), strong.as_ref())?;

            *file = FileHolder::Strong(strong);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Terminates the pager, stopping the port thread.
    pub fn terminate(&self) {
        {
            // Drop any remaining files outside of the lock context, since FxFile::drop will call
            // unregister_file which attempts to claim the lock again.
            let _files = std::mem::take(&mut self.inner.lock().unwrap().files);
        }

        self.port_thread.terminate();
    }

    /// Supplies pages in response to a page request.
    pub fn supply_pages(
        &self,
        vmo: &zx::Vmo,
        range: Range<u64>,
        transfer_vmo: &zx::Vmo,
        transfer_offset: u64,
    ) {
        if let Err(e) = self.pager.supply_pages(vmo, range, transfer_vmo, transfer_offset) {
            error!(error = e.as_value(), "supply_pages failed");
        }
    }

    pub fn report_failure(&self, vmo: &zx::Vmo, range: Range<u64>, status: zx::Status) {
        let pager_status = match status {
            zx::Status::IO_DATA_INTEGRITY => zx::Status::IO_DATA_INTEGRITY,
            // Shamelessly stolen from src/storage/blobfs/page_loader.h
            zx::Status::IO
            | zx::Status::IO_DATA_LOSS
            | zx::Status::IO_INVALID
            | zx::Status::IO_MISSED_DEADLINE
            | zx::Status::IO_NOT_PRESENT
            | zx::Status::IO_OVERRUN
            | zx::Status::IO_REFUSED
            | zx::Status::PEER_CLOSED => zx::Status::IO,
            _ => zx::Status::BAD_STATE,
        };
        if let Err(e) = self.pager.op_range(zx::PagerOp::Fail(pager_status), vmo, range) {
            error!(error = e.as_value(), "op_range failed");
        }
    }
}
