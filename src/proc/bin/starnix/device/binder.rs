// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]

use crate::device::DeviceOps;
use crate::fs::devtmpfs::dev_tmp_fs;
use crate::fs::{
    FdEvents, FileObject, FileOps, FileSystem, FileSystemHandle, FileSystemOps, FsNode, FsStr,
    NamespaceNode, ROMemoryDirectory, SeekOrigin, SpecialNode,
};
use crate::logging::not_implemented;
use crate::mm::{DesiredAddress, MappedVmo, MappingOptions, MemoryManager, UserMemoryCursor};
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::{CurrentTask, EventHandler, Kernel, WaitKey, WaitQueue, Waiter};
use crate::types::*;
use bitflags::bitflags;
use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
use slab::Slab;
use std::collections::{BTreeMap, VecDeque};
use std::sync::{Arc, Weak};
use zerocopy::{AsBytes, FromBytes};

/// The largest mapping of shared memory allowed by the binder driver.
const MAX_MMAP_SIZE: usize = 4 * 1024 * 1024;

/// Android's binder kernel driver implementation.
#[derive(Clone)]
pub struct BinderDev(Arc<BinderDriver>);

impl BinderDev {
    pub fn new() -> Self {
        Self(Arc::new(BinderDriver::new()))
    }
}

impl DeviceOps for BinderDev {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(self.clone()))
    }
}

impl FileOps for BinderDev {
    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        FdEvents::POLLIN | FdEvents::POLLOUT
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) -> WaitKey {
        self.0.wait_async(current_task, waiter, events, handler)
    }

    fn cancel_wait(&self, current_task: &CurrentTask, waiter: &Arc<Waiter>, key: WaitKey) -> bool {
        self.0.cancel_wait(current_task, waiter, key)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.0.ioctl(current_task, request, in_addr)
    }

    fn get_vmo(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        length: Option<usize>,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        self.0.get_vmo(length.unwrap_or(MAX_MMAP_SIZE))
    }

    fn mmap(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        addr: DesiredAddress,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        mapping_options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        self.0.mmap(file, current_task, addr, vmo_offset, length, flags, mapping_options, filename)
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }
    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn seek(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: off_t,
        _whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        error!(EOPNOTSUPP)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }
}

#[derive(Debug)]
struct BinderProcess {
    pid: pid_t,
    /// The [`SharedMemory`] region mapped in both the driver and the binder process. Allows for
    /// transactions to copy data once from the sender process into the receiver process.
    shared_memory: Mutex<Option<SharedMemory>>,
    /// The set of threads that are interacting with the binder driver.
    thread_pool: ThreadPool,
    /// Handle table of remote binder objects.
    handles: Mutex<HandleTable>,
}

impl BinderProcess {
    fn new(pid: pid_t) -> Self {
        Self {
            pid,
            shared_memory: Mutex::new(None),
            thread_pool: ThreadPool::new(),
            handles: Mutex::new(HandleTable::new()),
        }
    }
}

/// The mapped VMO shared between userspace and the binder driver.
///
/// The binder driver copies messages from one process to another, which essentially amounts to
/// a copy between VMOs. It is not possible to copy directly between VMOs without an intermediate
/// copy, and the binder driver must only perform one copy for performance reasons.
///
/// The memory allocated to a binder process is shared with the binder driver, and mapped into
/// the kernel's address space so that a VMO read operation can copy directly into the mapped VMO.
#[derive(Debug)]
struct SharedMemory {
    /// The address in kernel address space where the VMO is mapped.
    kernel_address: *mut u8,
    /// The address in user address space where the VMO is mapped.
    user_address: UserAddress,
    /// The length of the shared memory mapping.
    length: usize,
    /// The next free address in our bump allocator.
    next_free_offset: usize,
}

impl Drop for SharedMemory {
    fn drop(&mut self) {
        let kernel_root_vmar = fuchsia_runtime::vmar_root_self();

        // SAFETY: This object hands out references to the mapped memory, but the borrow checker
        // ensures correct lifetimes.
        let res = unsafe { kernel_root_vmar.unmap(self.kernel_address as usize, self.length) };
        match res {
            Ok(()) => {}
            Err(status) => {
                log::error!("failed to unmap shared binder region from kernel: {:?}", status);
            }
        }
    }
}

// SAFETY: SharedMemory has exclusive ownership of the `kernel_address` pointer, so it is safe to
// send across threads.
unsafe impl Send for SharedMemory {}

impl SharedMemory {
    fn map(vmo: &zx::Vmo, user_address: UserAddress, length: usize) -> Result<Self, Errno> {
        // Map the VMO into the kernel's address space.
        let kernel_root_vmar = fuchsia_runtime::vmar_root_self();
        let kernel_address = kernel_root_vmar
            .map(0, vmo, 0, length, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE)
            .map_err(|status| {
                log::error!("failed to map shared binder region in kernel: {:?}", status);
                errno!(ENOMEM)
            })?;
        Ok(Self {
            kernel_address: kernel_address as *mut u8,
            user_address,
            length,
            next_free_offset: 0,
        })
    }

    // This is a temporary implementation of an allocator and should be replaced by something
    // more sophisticated. It currently implements a bump allocator strategy.
    fn allocate_buffer<'a>(&'a mut self, length: usize) -> Result<SharedBuffer<'a>, Errno> {
        if let Some(offset) = self.next_free_offset.checked_add(length) {
            if offset <= self.length {
                let this_offset = self.next_free_offset;
                self.next_free_offset = offset;
                return Ok(SharedBuffer { memory: self, offset: this_offset, length });
            }
        }
        error!(ENOMEM)
    }
}

/// A buffer of memory allocated from a binder process' [`SharedMemory`].
#[derive(Debug)]
struct SharedBuffer<'a> {
    memory: &'a SharedMemory,
    // Offset into the shared memory region where this buffer begins.
    offset: usize,
    // The length of the buffer.
    length: usize,
}

impl<'a> SharedBuffer<'a> {
    fn as_mut_bytes(&mut self) -> &mut [u8] {
        // SAFETY: `offset + length` was bounds-checked by `allocate_buffer`, and the memory region
        // pointed to was zero-allocated by mapping a new VMO.
        unsafe {
            std::slice::from_raw_parts_mut(self.memory.kernel_address.add(self.offset), self.length)
        }
    }

    fn user_address(&self) -> UserAddress {
        self.memory.user_address + self.offset
    }
}

/// The set of threads that are interacting with the binder driver for a given process.
#[derive(Debug)]
struct ThreadPool(RwLock<BTreeMap<pid_t, Arc<BinderThread>>>);

impl ThreadPool {
    fn new() -> Self {
        Self(RwLock::new(BTreeMap::new()))
    }

    fn find_or_register_thread(&self, tid: pid_t) -> Arc<BinderThread> {
        self.0.write().entry(tid).or_insert_with(|| Arc::new(BinderThread::new(tid))).clone()
    }

    fn find_thread(&self, tid: pid_t) -> Result<Arc<BinderThread>, Errno> {
        self.0.read().get(&tid).cloned().ok_or_else(|| errno!(ENOENT))
    }

    /// Finds the first available binder thread that has registered with the driver.
    fn find_available_thread(&self) -> Option<Arc<BinderThread>> {
        self.0.read().iter().find_map(|(_, thread)| {
            if thread.state.read().intersects(ThreadState::BINDER_THREAD) {
                Some(thread.clone())
            } else {
                None
            }
        })
    }

    /// Finds the first available binder thread that can handle a transaction. The thread must not
    /// be in the middle of handling another transaction, and must be registered with the binder
    /// driver. If a thread is found, its state is marked as [`ThreadState::HANDLING_TRANSACTION`].
    fn find_thread_for_transaction(&self) -> Option<Arc<BinderThread>> {
        self.0.read().iter().find_map(|(_, thread)| {
            let mut state = thread.state.write();
            if state.intersects(ThreadState::BINDER_THREAD)
                && !state.contains(ThreadState::HANDLING_TRANSACTION)
            {
                state.insert(ThreadState::HANDLING_TRANSACTION);
                Some(thread.clone())
            } else {
                None
            }
        })
    }
}

/// Table containing handles to remote binder objects.
#[derive(Debug)]
struct HandleTable(Slab<BinderObjectProxy>);

impl HandleTable {
    fn new() -> Self {
        Self(Slab::new())
    }

    /// Inserts a new proxy to a remote binder object and retrieves a handle to it.
    #[cfg(test)]
    fn insert(&mut self, object: BinderObjectProxy) -> Handle {
        Handle::Object { index: self.0.insert(object) }
    }

    /// Retrieves the proxy to a binder object represented by `handle`. The special handle `0`
    /// ([`Handle::SpecialServiceManager`]) will fail.
    fn get(&self, handle: Handle) -> Result<&BinderObjectProxy, Errno> {
        match handle {
            Handle::SpecialServiceManager => error!(ENOENT),
            Handle::Object { index } => self.0.get(index).ok_or_else(|| errno!(ENOENT)),
        }
    }

    /// Retrieves the proxy to a binder object represented by `handle` and the owning process of the
    /// object. The special handle `0` ([`Handle::SpecialServiceManager`]) will fail.
    fn find_object_and_owner_for_handle(
        &self,
        handle: Handle,
    ) -> Result<(FlatBinderObject, Arc<BinderProcess>), Errno> {
        let proxy = self.get(handle)?;
        Ok((
            FlatBinderObject::Local { object: proxy.object.clone() },
            proxy.owner.upgrade().ok_or_else(|| errno!(ENOENT))?,
        ))
    }
}

#[derive(Debug)]
struct BinderThread {
    tid: pid_t,
    /// The state of the thread.
    state: RwLock<ThreadState>,
    /// The binder driver uses this queue to communicate with a binder thread. When a binder thread
    /// issues a [`BINDER_IOCTL_WRITE_READ`] ioctl, it will read from this command queue.
    command_queue: Mutex<VecDeque<Command>>,
    /// The stack of transactions that are active for this thread.
    transactions: RwLock<Vec<Transaction>>,
    /// When there are no commands in the command queue, the binder thread can register with this
    /// [`WaitQueue`] to be notified when commands are available.
    waiters: Mutex<WaitQueue>,
}

impl BinderThread {
    fn new(tid: pid_t) -> Self {
        Self {
            tid,
            state: RwLock::new(ThreadState::empty()),
            transactions: RwLock::new(Vec::new()),
            command_queue: Mutex::new(VecDeque::new()),
            waiters: Mutex::new(WaitQueue::default()),
        }
    }

    /// Enqueues `command` for the thread and wakes it up if necessary.
    fn enqueue_command(&self, command: Command) {
        self.command_queue.lock().push_back(command);
        self.waiters.lock().notify_events(FdEvents::POLLIN);
    }
}

bitflags! {
    /// The state of a thread.
    struct ThreadState: u32 {
        /// The thread is the main binder thread.
        const MAIN = 1;

        /// The thread is an auxiliary binder thread.
        const REGISTERED = 1 << 2;

        /// The thread is waiting for commands.
        const WAITING_FOR_COMMAND = 1 << 3;

        /// The thread is currently handling a transaction.
        const HANDLING_TRANSACTION = 1 << 4;

        /// The thread is a main or auxiliary binder thread.
        const BINDER_THREAD = Self::MAIN.bits | Self::REGISTERED.bits;
    }
}

impl Default for ThreadState {
    fn default() -> Self {
        ThreadState::empty()
    }
}

/// Whether a ref-count operation is strong or weak.
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
enum Ref {
    Strong,
    Weak,
}

/// Commands for a binder thread to execute.
#[derive(Debug)]
enum Command {
    /// Commands a binder thread to incrememnt the ref-count (strong/weak) of a binder object.
    AcquireRef(Ref, BinderObject),
    /// Commands a binder thread to decrement the ref-count (strong/weak) of a binder object.
    ReleaseRef(Ref, BinderObject),
    /// Commands a binder thread to start processing an incoming transaction from another binder
    /// process.
    Transaction(Transaction),
    /// Commands a binder thread to process an incoming reply to its transaction.
    Reply(Transaction),
    /// Notifies a binder thread that a transaction has completed.
    TransactionComplete,
}

impl Command {
    /// Returns the command's BR_* code for serialization.
    fn driver_return_code(&self) -> binder_driver_return_protocol {
        match self {
            Command::AcquireRef(Ref::Strong, ..) => binder_driver_return_protocol_BR_ACQUIRE,
            Command::AcquireRef(Ref::Weak, ..) => binder_driver_return_protocol_BR_INCREFS,
            Command::ReleaseRef(Ref::Strong, ..) => binder_driver_return_protocol_BR_RELEASE,
            Command::ReleaseRef(Ref::Weak, ..) => binder_driver_return_protocol_BR_DECREFS,
            Command::Transaction(..) => binder_driver_return_protocol_BR_TRANSACTION,
            Command::Reply(..) => binder_driver_return_protocol_BR_REPLY,
            Command::TransactionComplete => binder_driver_return_protocol_BR_TRANSACTION_COMPLETE,
        }
    }

    /// Serializes and writes the command into userspace memory at `buffer`.
    fn write_to_memory(&self, mm: &MemoryManager, buffer: &UserBuffer) -> Result<usize, Errno> {
        match self {
            Command::AcquireRef(_, obj) | Command::ReleaseRef(_, obj) => {
                #[repr(C, packed)]
                #[derive(AsBytes)]
                struct AcquireRefData {
                    command: binder_driver_return_protocol,
                    weak_ref_addr: u64,
                    strong_ref_addr: u64,
                }
                if buffer.length < std::mem::size_of::<AcquireRefData>() {
                    return error!(ENOMEM);
                }
                mm.write_object(
                    UserRef::new(buffer.address),
                    &AcquireRefData {
                        command: self.driver_return_code(),
                        weak_ref_addr: obj.weak_ref_addr.ptr() as u64,
                        strong_ref_addr: obj.strong_ref_addr.ptr() as u64,
                    },
                )
            }
            Command::Transaction(transaction) | Command::Reply(transaction) => {
                #[repr(C, packed)]
                #[derive(AsBytes)]
                struct TransactionData {
                    command: binder_driver_return_protocol,
                    transaction: [u8; std::mem::size_of::<binder_transaction_data>()],
                }
                if buffer.length < std::mem::size_of::<TransactionData>() {
                    return error!(ENOMEM);
                }
                mm.write_object(
                    UserRef::new(buffer.address),
                    &TransactionData {
                        command: self.driver_return_code(),
                        transaction: transaction.as_bytes(),
                    },
                )
            }
            Command::TransactionComplete => {
                if buffer.length < std::mem::size_of::<binder_driver_return_protocol>() {
                    return error!(ENOMEM);
                }
                mm.write_object(UserRef::new(buffer.address), &self.driver_return_code())
            }
        }
    }
}

/// Proxy of a binder object
#[derive(Debug, Clone)]
struct BinderObjectProxy {
    /// The owner of the binder object.
    owner: Weak<BinderProcess>,
    /// The underlying binder object.
    object: BinderObject,
}

/// A binder object.
/// All addresses are in the owning process' address space.
#[derive(Debug, Clone, Eq, PartialEq)]
struct BinderObject {
    /// Address to the weak ref-count structure. Guaranteed to exist.
    weak_ref_addr: UserAddress,
    /// Address to the strong ref-count structure (actual object). May not exist if the object was
    /// destroyed.
    strong_ref_addr: UserAddress,
}

/// Non-union version of [`binder_transaction_data`].
#[derive(Debug)]
struct Transaction {
    peer_pid: pid_t,
    peer_tid: pid_t,
    peer_euid: u32,

    object: FlatBinderObject,
    code: u32,
    flags: u32,

    data_buffer: UserBuffer,
    offsets_buffer: UserBuffer,
}

impl Transaction {
    fn as_bytes(&self) -> [u8; std::mem::size_of::<binder_transaction_data>()] {
        match self.object {
            FlatBinderObject::Remote { handle } => {
                struct_with_union_into_bytes!(binder_transaction_data {
                    target.handle: handle.into(),
                    cookie: 0,
                    code: self.code,
                    flags: self.flags,
                    sender_pid: self.peer_pid,
                    sender_euid: self.peer_euid,
                    data_size: self.data_buffer.length as u64,
                    offsets_size: self.offsets_buffer.length as u64,
                    data.ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                        buffer: self.data_buffer.address.ptr() as u64,
                        offsets: self.offsets_buffer.address.ptr() as u64,
                    },
                })
            }
            FlatBinderObject::Local { ref object } => {
                struct_with_union_into_bytes!(binder_transaction_data {
                    target.ptr: object.weak_ref_addr.ptr() as u64,
                    cookie: object.strong_ref_addr.ptr() as u64,
                    code: self.code,
                    flags: self.flags,
                    sender_pid: self.peer_pid,
                    sender_euid: self.peer_euid,
                    data_size: self.data_buffer.length as u64,
                    offsets_size: self.offsets_buffer.length as u64,
                    data.ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                        buffer: self.data_buffer.address.ptr() as u64,
                        offsets: self.offsets_buffer.address.ptr() as u64,
                    },
                })
            }
        }
    }
}

/// Non-union version of [`flat_binder_object`].
#[derive(Debug)]
enum FlatBinderObject {
    Local { object: BinderObject },
    Remote { handle: Handle },
}

/// A handle to a binder object.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Handle {
    /// Special handle `0` to the binder `IServiceManager` object within the context manager
    /// process.
    SpecialServiceManager,
    /// A handle to a binder object in another process.
    Object {
        /// The index of the binder object in a process' handle table.
        /// This is `handle - 1`, because the special handle 0 is reserved.
        index: usize,
    },
}

impl From<u32> for Handle {
    fn from(handle: u32) -> Self {
        if handle == 0 {
            Handle::SpecialServiceManager
        } else {
            Handle::Object { index: handle as usize - 1 }
        }
    }
}

impl From<Handle> for u32 {
    fn from(handle: Handle) -> Self {
        match handle {
            Handle::SpecialServiceManager => 0,
            Handle::Object { index } => (index as u32) + 1,
        }
    }
}

/// The ioctl character for all binder ioctls.
const BINDER_IOCTL_CHAR: u8 = b'b';

/// The ioctl for initiating a read-write transaction.
const BINDER_IOCTL_WRITE_READ: u32 =
    encode_ioctl_write_read::<binder_write_read>(BINDER_IOCTL_CHAR, 1);

/// The ioctl for setting the maximum number of threads the process wants to create for binder work.
const BINDER_IOCTL_SET_MAX_THREADS: u32 = encode_ioctl_write::<u32>(BINDER_IOCTL_CHAR, 5);

/// The ioctl for requests to become context manager.
const BINDER_IOCTL_SET_CONTEXT_MGR: u32 = encode_ioctl_write::<u32>(BINDER_IOCTL_CHAR, 7);

/// The ioctl for retrieving the kernel binder version.
const BINDER_IOCTL_VERSION: u32 = encode_ioctl_write_read::<binder_version>(BINDER_IOCTL_CHAR, 9);

/// The ioctl for requests to become context manager, v2.
const BINDER_IOCTL_SET_CONTEXT_MGR_EXT: u32 =
    encode_ioctl_write::<flat_binder_object>(BINDER_IOCTL_CHAR, 13);

// The ioctl for enabling one-way transaction spam detection.
const BINDER_IOCTL_ENABLE_ONEWAY_SPAM_DETECTION: u32 =
    encode_ioctl_write::<u32>(BINDER_IOCTL_CHAR, 16);

struct BinderDriver {
    /// The "name server" process that is addressed via the special handle 0 and is responsible
    /// for implementing the binder protocol `IServiceManager`.
    context_manager: RwLock<Option<Weak<BinderProcess>>>,

    /// Manages the internal state of each process interacting with the binder driver.
    procs: RwLock<BTreeMap<pid_t, Arc<BinderProcess>>>,
}

impl BinderDriver {
    fn new() -> Self {
        Self { context_manager: RwLock::new(None), procs: RwLock::new(BTreeMap::new()) }
    }

    fn find_process(&self, pid: pid_t) -> Result<Arc<BinderProcess>, Errno> {
        self.procs.read().get(&pid).cloned().ok_or_else(|| errno!(ENOENT))
    }

    fn find_or_register_process(&self, pid: pid_t) -> Arc<BinderProcess> {
        self.procs.write().entry(pid).or_insert_with(|| Arc::new(BinderProcess::new(pid))).clone()
    }

    fn get_context_manager(&self) -> Result<Arc<BinderProcess>, Errno> {
        self.context_manager.read().as_ref().and_then(Weak::upgrade).ok_or_else(|| errno!(ENOENT))
    }

    // Find the binder object and owning process referred to by `handle`.
    fn find_object_and_owner_for_handle(
        &self,
        binder_proc: &BinderProcess,
        handle: Handle,
    ) -> Result<(FlatBinderObject, Arc<BinderProcess>), Errno> {
        match handle {
            Handle::SpecialServiceManager => {
                // This handle (0) always refers to the context manager, which is always "remote",
                // even for the context manager itself.
                Ok((
                    FlatBinderObject::Remote { handle: Handle::SpecialServiceManager },
                    self.get_context_manager()?,
                ))
            }
            handle => binder_proc.handles.lock().find_object_and_owner_for_handle(handle),
        }
    }

    fn ioctl(
        &self,
        current_task: &CurrentTask,
        request: u32,
        user_arg: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            BINDER_IOCTL_VERSION => {
                // A thread is requesting the version of this binder driver.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }
                let response =
                    binder_version { protocol_version: BINDER_CURRENT_PROTOCOL_VERSION as i32 };
                current_task.mm.write_object(UserRef::new(user_arg), &response)?;
                Ok(SUCCESS)
            }
            BINDER_IOCTL_SET_CONTEXT_MGR | BINDER_IOCTL_SET_CONTEXT_MGR_EXT => {
                // A process is registering itself as the context manager.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }

                // TODO: Read the flat_binder_object when ioctl is BINDER_IOCTL_SET_CONTEXT_MGR_EXT.

                let binder_proc = self.find_or_register_process(current_task.get_pid());
                *self.context_manager.write() = Some(Arc::downgrade(&binder_proc));
                Ok(SUCCESS)
            }
            BINDER_IOCTL_WRITE_READ => {
                // A thread is requesting to exchange data with the binder driver.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }

                let user_ref = UserRef::new(user_arg);
                let mut input = binder_write_read::new_zeroed();
                current_task.mm.read_object(user_ref, &mut input)?;

                let binder_proc = self.find_or_register_process(current_task.get_pid());
                let binder_thread =
                    binder_proc.thread_pool.find_or_register_thread(current_task.get_tid());

                // We will be writing this back to userspace, don't trust what the client gave us.
                input.write_consumed = 0;
                input.read_consumed = 0;

                if input.write_size > 0 {
                    // The calling thread wants to write some data to the binder driver.
                    let mut cursor = UserMemoryCursor::new(
                        &*current_task.mm,
                        UserAddress::from(input.write_buffer),
                        input.write_size,
                    );

                    // Handle all the data the calling thread sent, which may include multiple
                    // commands.
                    while cursor.bytes_read() < input.write_size as usize {
                        self.handle_thread_write(
                            current_task,
                            &binder_proc,
                            &binder_thread,
                            &mut cursor,
                        )?;
                    }
                    input.write_consumed = cursor.bytes_read() as u64;
                }

                if input.read_size > 0 {
                    // The calling thread wants to read some data from the binder driver, blocking
                    // if there is nothing immediately available.
                    let read_buffer = UserBuffer {
                        address: UserAddress::from(input.read_buffer),
                        length: input.read_size as usize,
                    };
                    input.read_consumed =
                        self.handle_thread_read(current_task, &*binder_thread, &read_buffer)?
                            as u64;
                }

                // Write back to the calling thread how much data was read/written.
                current_task.mm.write_object(user_ref, &input)?;
                Ok(SUCCESS)
            }
            BINDER_IOCTL_SET_MAX_THREADS => {
                not_implemented!("binder ignoring SET_MAX_THREADS ioctl");
                Ok(SUCCESS)
            }
            BINDER_IOCTL_ENABLE_ONEWAY_SPAM_DETECTION => {
                not_implemented!("binder ignoring ENABLE_ONEWAY_SPAM_DETECTION ioctl");
                Ok(SUCCESS)
            }
            _ => {
                log::error!("binder received unknown ioctl request 0x{:08x}", request);
                error!(EINVAL)
            }
        }
    }

    /// Consumes one work item from the userspace binder_write_read buffer and handles it.
    /// This method will never block.
    fn handle_thread_write<'a>(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        cursor: &mut UserMemoryCursor<'a>,
    ) -> Result<(), Errno> {
        let command = cursor.read_object::<binder_driver_command_protocol>()?;
        match command {
            binder_driver_command_protocol_BC_ENTER_LOOPER
            | binder_driver_command_protocol_BC_REGISTER_LOOPER => {
                self.handle_looper_registration(command, binder_thread)
            }
            binder_driver_command_protocol_BC_INCREFS
            | binder_driver_command_protocol_BC_ACQUIRE
            | binder_driver_command_protocol_BC_DECREFS
            | binder_driver_command_protocol_BC_RELEASE => {
                self.handle_refcount_operation(command, binder_proc, cursor)
            }
            binder_driver_command_protocol_BC_INCREFS_DONE
            | binder_driver_command_protocol_BC_ACQUIRE_DONE => {
                self.handle_refcount_operation_done(command, binder_thread, cursor)
            }
            binder_driver_command_protocol_BC_TRANSACTION => {
                self.handle_transaction(current_task, binder_proc, binder_thread, cursor)
            }
            binder_driver_command_protocol_BC_REPLY => {
                self.handle_reply(current_task, binder_proc, binder_thread, cursor)
            }
            _ => {
                log::error!("binder received unknown RW command: 0x{:08x}", command);
                error!(EINVAL)
            }
        }
    }

    /// Handle a binder thread's request to register itself with the binder driver.
    /// This makes the binder thread eligible for receiving commands from the driver.
    fn handle_looper_registration(
        &self,
        command: binder_driver_command_protocol,
        binder_thread: &Arc<BinderThread>,
    ) -> Result<(), Errno> {
        let mut state = binder_thread.state.write();
        if state.intersects(ThreadState::BINDER_THREAD) {
            // This thread is already registered.
            error!(EINVAL)
        } else {
            *state |= match command {
                binder_driver_command_protocol_BC_ENTER_LOOPER => ThreadState::MAIN,
                binder_driver_command_protocol_BC_REGISTER_LOOPER => ThreadState::REGISTERED,
                _ => unreachable!(),
            };
            Ok(())
        }
    }

    /// Handle a binder thread's request to increment/decrement a strong/weak reference to a remote
    /// binder object.
    fn handle_refcount_operation<'a>(
        &self,
        command: binder_driver_command_protocol,
        binder_proc: &Arc<BinderProcess>,
        cursor: &mut UserMemoryCursor<'a>,
    ) -> Result<(), Errno> {
        // TODO: Keep track of reference counts internally and only send a command to the owning
        // binder process on the first increment/last decrement.
        let handle = cursor.read_object::<u32>()?.into();

        // Find the process that owns the remote object.
        let (object, target_proc) = self.find_object_and_owner_for_handle(binder_proc, handle)?;
        let object = match object {
            FlatBinderObject::Remote { .. } => {
                // TODO: Figure out how to acquire/release refs for the context manager
                // object.
                not_implemented!("acquire/release refs for context manager object");
                return Ok(());
            }
            FlatBinderObject::Local { object } => object,
        };

        // Select a thread to handle this refcount task.
        let target_thread = target_proc.thread_pool.find_available_thread().expect(concat!(
            "no thread available for command. ",
            "process-level command queuing not implemented."
        ));

        target_thread.enqueue_command(match command {
            binder_driver_command_protocol_BC_INCREFS => Command::AcquireRef(Ref::Weak, object),
            binder_driver_command_protocol_BC_ACQUIRE => Command::AcquireRef(Ref::Strong, object),
            binder_driver_command_protocol_BC_DECREFS => Command::ReleaseRef(Ref::Weak, object),
            binder_driver_command_protocol_BC_RELEASE => Command::ReleaseRef(Ref::Strong, object),
            _ => unreachable!(),
        });

        Ok(())
    }

    /// Handle a binder thread's notification that it successfully incremented a strong/weak
    /// reference to a local (in-process) binder object. This is in response to a
    /// `BR_ACQUIRE`/`BR_INCREFS` command.
    fn handle_refcount_operation_done<'a>(
        &self,
        command: binder_driver_command_protocol,
        binder_thread: &Arc<BinderThread>,
        cursor: &mut UserMemoryCursor<'a>,
    ) -> Result<(), Errno> {
        // TODO: When the binder driver keeps track of references internally, this should
        // reduce the temporary refcount that is held while the binder thread performs the
        // refcount.
        let object = BinderObject {
            weak_ref_addr: UserAddress::from(cursor.read_object::<binder_uintptr_t>()?),
            strong_ref_addr: UserAddress::from(cursor.read_object::<binder_uintptr_t>()?),
        };
        let msg = match command {
            binder_driver_command_protocol_BC_INCREFS_DONE => "BC_INCREFS_DONE",
            binder_driver_command_protocol_BC_ACQUIRE_DONE => "BC_ACQUIRE_DONE",
            _ => unreachable!(),
        };
        not_implemented!("binder thread {} {} {:?}", binder_thread.tid, msg, &object);
        Ok(())
    }

    /// A binder thread is starting a transaction on a remote binder object.
    fn handle_transaction<'a>(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        cursor: &mut UserMemoryCursor<'a>,
    ) -> Result<(), Errno> {
        let data = cursor.read_object::<binder_transaction_data>()?;

        // SAFETY: Transactions can only refer to handles.
        let handle = unsafe { data.target.handle }.into();

        let (object, target_proc) = self.find_object_and_owner_for_handle(binder_proc, handle)?;

        // Copy the transaction data to the target process.
        let (data_buffer, offsets_buffer) =
            self.copy_transaction_buffers(current_task, &target_proc, &data)?;

        // Find a thread to handle the transaction.
        // TODO: Deal with no threads available.
        let target_thread = target_proc.thread_pool.find_thread_for_transaction().unwrap();

        let target_euid = current_task
            .kernel()
            .pids
            .read()
            .get_task(target_thread.tid)
            .map(|t| t.creds.read().euid)
            .ok_or_else(|| errno!(ENOENT))?;

        if data.flags & transaction_flags_TF_ONE_WAY != 0 {
            // The caller is not expecting a reply.
            binder_thread.enqueue_command(Command::TransactionComplete);
        } else {
            // Create a new transaction on the sender so that they can wait on a reply.
            binder_thread.transactions.write().push(Transaction {
                peer_pid: target_proc.pid,
                peer_tid: target_thread.tid,
                peer_euid: target_euid,

                object: FlatBinderObject::Remote { handle },
                code: data.code,
                flags: data.flags,

                data_buffer: UserBuffer::default(),
                offsets_buffer: UserBuffer::default(),
            });
        }

        // Write the transaction to the target process.
        target_thread.enqueue_command(Command::Transaction(Transaction {
            peer_pid: binder_proc.pid,
            peer_tid: binder_thread.tid,
            peer_euid: current_task.creds.read().euid,

            object,
            code: data.code,
            flags: data.flags,

            data_buffer,
            offsets_buffer,
        }));
        Ok(())
    }

    /// A binder thread is sending a reply to a transaction.
    fn handle_reply<'a>(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        cursor: &mut UserMemoryCursor<'a>,
    ) -> Result<(), Errno> {
        let data = cursor.read_object::<binder_transaction_data>()?;

        // Find the process and thread that initiated the transaction. This reply is for them.
        let (target_proc, target_thread) = {
            let mut transactions = binder_thread.transactions.write();
            let transaction = transactions.last_mut().ok_or_else(|| errno!(EINVAL))?;
            let target_proc = self.find_process(transaction.peer_pid)?;
            let target_thread = target_proc.thread_pool.find_thread(transaction.peer_tid)?;
            (target_proc, target_thread)
        };

        // Copy the transaction data to the target process.
        let (data_buffer, offsets_buffer) =
            self.copy_transaction_buffers(current_task, &target_proc, &data)?;

        // Schedule the transaction on the target process' command queue.
        target_thread.enqueue_command(Command::Reply(Transaction {
            peer_pid: binder_proc.pid,
            peer_tid: binder_thread.tid,
            peer_euid: current_task.creds.read().euid,

            object: FlatBinderObject::Remote { handle: Handle::SpecialServiceManager },
            code: data.code,
            flags: data.flags,

            data_buffer,
            offsets_buffer,
        }));

        // Schedule the transaction complete command on the caller's command queue.
        binder_thread.enqueue_command(Command::TransactionComplete);

        Ok(())
    }

    /// Dequeues a command from the thread's command queue, or blocks until commands are available.
    fn handle_thread_read(
        &self,
        current_task: &CurrentTask,
        binder_thread: &BinderThread,
        read_buffer: &UserBuffer,
    ) -> Result<usize, Errno> {
        loop {
            let mut command_queue = binder_thread.command_queue.lock();
            if let Some(command) = command_queue.front() {
                // Attempt to write the command to the thread's buffer.
                let bytes_written = command.write_to_memory(&current_task.mm, read_buffer)?;

                // SAFETY: There is an item in the queue since we're in the `Some` branch.
                match command_queue.pop_front().unwrap() {
                    Command::Transaction(t) => {
                        // A transaction has begun, push it onto the transaction stack.
                        binder_thread.transactions.write().push(t);
                    }
                    Command::Reply(_) | Command::TransactionComplete => {
                        // A transaction is complete, pop it from the transaction stack.
                        let mut state = binder_thread.state.write();
                        let mut transactions = binder_thread.transactions.write();
                        transactions.pop();
                        if transactions.is_empty() {
                            state.remove(ThreadState::HANDLING_TRANSACTION);
                        }
                    }
                    Command::AcquireRef(_, _) | Command::ReleaseRef(_, _) => {}
                }

                return Ok(bytes_written);
            }

            // No commands readily available to read. Register with the wait queue.
            let waiter = Waiter::new();
            binder_thread.waiters.lock().wait_async_events(
                &waiter,
                FdEvents::POLLIN,
                Box::new(|_| {}),
            );
            drop(command_queue);

            // Put this thread to sleep.
            scopeguard::defer! {
                binder_thread.state.write().remove(ThreadState::WAITING_FOR_COMMAND);
            }
            binder_thread.state.write().insert(ThreadState::WAITING_FOR_COMMAND);
            waiter.wait(current_task)?;
        }
    }

    /// Copies transaction buffers from the source process' address space to a new buffer in the
    /// target process' shared binder VMO.
    /// Returns a pair of addresses, the first the address to the transaction data, the second the
    /// address to the offset buffer.
    fn copy_transaction_buffers(
        &self,
        current_task: &CurrentTask,
        target_proc: &Arc<BinderProcess>,
        data: &binder_transaction_data,
    ) -> Result<(UserBuffer, UserBuffer), Errno> {
        // Get the shared memory of the target process.
        let mut shared_memory_lock = target_proc.shared_memory.lock();
        let shared_memory = shared_memory_lock.as_mut().ok_or_else(|| errno!(ENOMEM))?;

        let data_size = data.data_size as usize;
        let offsets_size = data.offsets_size as usize;

        // The total buffer size we need to allocate is the data + offsets.
        let total_size = data_size.checked_add(offsets_size).ok_or_else(|| errno!(EINVAL))?;

        // Allocate a buffer from the target process' shared memory.
        let mut shared_buffer = shared_memory.allocate_buffer(total_size)?;
        let (data_buffer, offsets_buffer) = shared_buffer.as_mut_bytes().split_at_mut(data_size);

        // SAFETY: `binder_transaction_data` was read from a userspace VMO, which means that all
        // bytes are defined, making union access safe.
        let userspace_addrs = unsafe { data.data.ptr };

        // Copy the data straight into the target's buffer.
        current_task.mm.read_memory(UserAddress::from(userspace_addrs.buffer), data_buffer)?;
        current_task.mm.read_memory(UserAddress::from(userspace_addrs.offsets), offsets_buffer)?;

        let data_buffer = UserBuffer { address: shared_buffer.user_address(), length: data_size };
        let offsets_buffer =
            UserBuffer { address: shared_buffer.user_address() + data_size, length: offsets_size };
        Ok((data_buffer, offsets_buffer))
    }

    fn get_vmo(&self, length: usize) -> Result<zx::Vmo, Errno> {
        zx::Vmo::create(length as u64).map_err(|_| errno!(ENOMEM))
    }

    fn mmap(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        addr: DesiredAddress,
        _vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        mapping_options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        let binder_proc = self.find_or_register_process(current_task.get_pid());

        // Do not support mapping shared memory more than once.
        let mut shared_memory = binder_proc.shared_memory.lock();
        if shared_memory.is_some() {
            return error!(EINVAL);
        }

        // Create a VMO that will be shared between the driver and the client process.
        let vmo = Arc::new(self.get_vmo(length)?);

        // Map the VMO into the binder process' address space.
        let user_address = current_task.mm.map(
            addr,
            vmo.clone(),
            0,
            length,
            flags,
            mapping_options,
            Some(filename),
        )?;

        // Map the VMO into the driver's address space.
        match SharedMemory::map(&*vmo, user_address, length) {
            Ok(mem) => {
                *shared_memory = Some(mem);
                Ok(MappedVmo::new(vmo, user_address))
            }
            Err(err) => {
                // Try to cleanup by unmapping from userspace, but ignore any errors. We
                // can't really recover from them.
                let _ = current_task.mm.unmap(user_address, length);
                Err(err)
            }
        }
    }

    fn wait_async(
        &self,
        current_task: &CurrentTask,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) -> WaitKey {
        let binder_proc = self.find_or_register_process(current_task.get_pid());
        let binder_thread = binder_proc.thread_pool.find_or_register_thread(current_task.get_tid());
        let command_queue = binder_thread.command_queue.lock();
        if command_queue.is_empty() {
            binder_thread.waiters.lock().wait_async_events(waiter, events, handler)
        } else {
            waiter.wake_immediately(FdEvents::POLLIN.mask(), handler)
        }
    }

    fn cancel_wait(&self, current_task: &CurrentTask, _waiter: &Arc<Waiter>, key: WaitKey) -> bool {
        let binder_proc = self.find_or_register_process(current_task.get_pid());
        let binder_thread = binder_proc.thread_pool.find_or_register_thread(current_task.get_tid());
        let result = binder_thread.waiters.lock().cancel_wait(key);
        result
    }
}

pub struct BinderFs(());
impl FileSystemOps for BinderFs {}

const BINDERS: &[&'static FsStr] = &[b"binder", b"hwbinder", b"vndbinder"];

impl BinderFs {
    pub fn new(kernel: &Kernel) -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(BinderFs(()));
        fs.set_root(ROMemoryDirectory);
        for binder in BINDERS {
            BinderFs::add_binder(kernel, &fs, &binder)?;
        }
        Ok(fs)
    }

    fn add_binder(kernel: &Kernel, fs: &FileSystemHandle, name: &FsStr) -> Result<(), Errno> {
        let dev = kernel.device_registry.write().register_misc_chrdev(BinderDev::new())?;
        fs.root().add_node_ops_dev(name, mode!(IFCHR, 0o600), dev, SpecialNode)?;
        Ok(())
    }
}

pub fn create_binders(kernel: &Kernel) -> Result<(), Errno> {
    for binder in BINDERS {
        BinderFs::add_binder(kernel, dev_tmp_fs(kernel), binder)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mm::PAGE_SIZE;
    use crate::testing::*;
    use assert_matches::assert_matches;

    #[test]
    fn handle_tests() {
        assert_matches!(Handle::from(0), Handle::SpecialServiceManager);
        assert_matches!(Handle::from(1), Handle::Object { index: 0 });
        assert_matches!(Handle::from(2), Handle::Object { index: 1 });
        assert_matches!(Handle::from(99), Handle::Object { index: 98 });
    }

    #[test]
    fn handle_0_fails_when_context_manager_is_not_set() {
        let driver = BinderDriver::new();
        let binder_proc = driver.find_or_register_process(1);
        assert_eq!(
            driver
                .find_object_and_owner_for_handle(&*binder_proc, 0.into())
                .expect_err("unexpectedly succeeded"),
            errno!(ENOENT),
        );
    }

    #[test]
    fn handle_0_succeeds_when_context_manager_is_set() {
        let driver = BinderDriver::new();
        let context_manager = driver.find_or_register_process(1);
        *driver.context_manager.write() = Some(Arc::downgrade(&context_manager));
        let binder_proc = driver.find_or_register_process(2);
        let (object, owning_proc) = driver
            .find_object_and_owner_for_handle(&*binder_proc, 0.into())
            .expect("failed to find handle 0");
        assert_matches!(object, FlatBinderObject::Remote { handle: Handle::SpecialServiceManager });
        assert!(Arc::ptr_eq(&context_manager, &owning_proc));
    }

    #[test]
    fn fail_to_retrieve_non_existing_handle() {
        let driver = BinderDriver::new();
        let binder_proc = driver.find_or_register_process(1);
        assert_eq!(
            driver
                .find_object_and_owner_for_handle(&*binder_proc, 3.into())
                .expect_err("unexpectedly succeeded"),
            errno!(ENOENT),
        );
    }

    #[test]
    fn retrieve_existing_handle() {
        let driver = BinderDriver::new();
        let proc_1 = driver.find_or_register_process(1);
        let proc_2 = driver.find_or_register_process(2);
        let expected_object = BinderObject {
            weak_ref_addr: UserAddress::from(0xffffffffffffffff),
            strong_ref_addr: UserAddress::from(0x1111111111111111),
        };
        let handle = proc_2.handles.lock().insert(BinderObjectProxy {
            owner: Arc::downgrade(&proc_1),
            object: expected_object.clone(),
        });
        let (object, owning_proc) = driver
            .find_object_and_owner_for_handle(&*proc_2, handle)
            .expect("failed to find handle");
        assert_matches!(object, FlatBinderObject::Local { object } if object == expected_object);
        assert!(Arc::ptr_eq(&proc_1, &owning_proc));
    }

    #[test]
    fn shared_memory_allocates_multiple_buffers() {
        const BASE_ADDR: UserAddress = UserAddress::from(0x000000000000000f);
        const VMO_LENGTH: usize = 4096;

        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        // Check that two buffers allocated from the same shared memory region don't overlap.
        const BUF1_LEN: usize = 64;
        let mut buf = shared_memory.allocate_buffer(BUF1_LEN).expect("allocate buffer 1");
        assert_eq!(buf.user_address(), BASE_ADDR);
        assert_eq!(buf.as_mut_bytes().len(), BUF1_LEN);
        buf.as_mut_bytes().fill(0xff);

        const BUF2_LEN: usize = 32;
        let mut buf = shared_memory.allocate_buffer(BUF2_LEN).expect("allocate buffer 2");
        assert_eq!(buf.user_address(), BASE_ADDR + BUF1_LEN);
        assert_eq!(buf.as_mut_bytes().len(), BUF2_LEN);
        buf.as_mut_bytes().fill(0xaa);

        // Check that the correct bit patterns were written through to the underlying VMO.
        let mut data = [0u8; BUF1_LEN];
        vmo.read(&mut data, 0).expect("read VMO failed");
        assert!(data.iter().all(|b| *b == 0xff));

        let mut data = [0u8; BUF2_LEN];
        vmo.read(&mut data, BUF1_LEN as u64).expect("read VMO failed");
        assert!(data.iter().all(|b| *b == 0xaa));
    }

    #[test]
    fn shared_memory_too_large_allocation_fails() {
        const BASE_ADDR: UserAddress = UserAddress::from(0x000000000000000f);
        const VMO_LENGTH: usize = 4096;

        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        shared_memory.allocate_buffer(VMO_LENGTH + 1).expect_err("out-of-bounds allocation");

        let _ = shared_memory.allocate_buffer(VMO_LENGTH).expect("allocate buffer");
        shared_memory.allocate_buffer(1).expect_err("out-of-bounds allocation");
    }

    #[fuchsia::test]
    async fn copy_transaction_data_between_processes() {
        let (_kernel, task1) = create_kernel_and_task();
        let driver = BinderDriver::new();

        // Register a binder process that represents `task1`. This is the source process: data will
        // be copied out of process ID 1 into process ID 2's shared memory.
        let _ = driver.find_or_register_process(1);

        // Initialize process 2 with shared memory in the driver.
        let proc2 = driver.find_or_register_process(2);
        const BASE_ADDR: UserAddress = UserAddress::from(0x000000000000000f);
        const VMO_LENGTH: usize = 4096;
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        *proc2.shared_memory.lock() = Some(
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory"),
        );

        // Map some memory for process 1.
        let data_addr = map_memory(&task1, UserAddress::default(), *PAGE_SIZE);

        // Write transaction data in process 1.
        const BINDER_DATA: &[u8; 8] = b"binder!!";
        let mut transaction_data = Vec::new();
        transaction_data.extend(BINDER_DATA);
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr: binder_object_header { type_: BINDER_TYPE_HANDLE },
            flags: 0,
            __bindgen_anon_1.handle: 0,
            cookie: 0,
        }));

        let offsets_addr = data_addr
            + task1
                .mm
                .write_memory(data_addr, &transaction_data)
                .expect("failed to write transaction data");

        // Write the offsets data (where in the data buffer `flat_binder_object`s are).
        let offsets_data: u64 = BINDER_DATA.len() as u64;
        task1
            .mm
            .write_object(UserRef::new(offsets_addr), &offsets_data)
            .expect("failed to write offsets buffer");

        // Construct the `binder_transaction_data` struct that contains pointers to the data and
        // offsets buffers.
        let transaction = binder_transaction_data {
            code: 1,
            flags: 0,
            sender_pid: 1,
            sender_euid: 0,
            target: binder_transaction_data__bindgen_ty_1 { handle: 0 },
            cookie: 0,
            data_size: transaction_data.len() as u64,
            offsets_size: std::mem::size_of::<u64>() as u64,
            data: binder_transaction_data__bindgen_ty_2 {
                ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                    buffer: data_addr.ptr() as u64,
                    offsets: offsets_addr.ptr() as u64,
                },
            },
        };

        // Copy the data from process 1 to process 2
        let (data_buffer, offsets_buffer) =
            driver.copy_transaction_buffers(&task1, &proc2, &transaction).expect("copy data");

        // Check that the returned buffers are in-bounds of process 2's shared memory.
        assert!(data_buffer.address >= BASE_ADDR);
        assert!(data_buffer.address < BASE_ADDR + VMO_LENGTH);
        assert!(offsets_buffer.address >= BASE_ADDR);
        assert!(offsets_buffer.address < BASE_ADDR + VMO_LENGTH);

        // Verify the contents of the copied data in process 2's shared memory VMO.
        let mut buffer = [0u8; BINDER_DATA.len() + std::mem::size_of::<flat_binder_object>()];
        vmo.read(&mut buffer, (data_buffer.address - BASE_ADDR) as u64)
            .expect("failed to read data");
        assert_eq!(&buffer[..], &transaction_data);

        let mut buffer = [0u8; std::mem::size_of::<u64>()];
        vmo.read(&mut buffer, (offsets_buffer.address - BASE_ADDR) as u64)
            .expect("failed to read offsets");
        assert_eq!(&buffer[..], offsets_data.as_bytes());
    }
}
