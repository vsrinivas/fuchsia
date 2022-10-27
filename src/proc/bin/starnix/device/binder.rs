// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]

use crate::device::DeviceOps;
use crate::fs::devtmpfs::dev_tmp_fs;
use crate::fs::{
    fs_node_impl_dir_readonly, DirEntryHandle, FdEvents, FdNumber, FileObject, FileOps, FileSystem,
    FileSystemHandle, FileSystemOps, FsNode, FsNodeOps, FsStr, MemoryDirectoryFile, NamespaceNode,
    SeekOrigin, SpecialNode, WaitAsyncOptions,
};
use crate::lock::{Mutex, MutexGuard, RwLock, RwLockReadGuard, RwLockWriteGuard};
use crate::logging::{not_implemented, not_implemented_log_once};
use crate::mm::vmo::round_up_to_increment;
use crate::mm::{DesiredAddress, MappedVmo, MappingOptions, MemoryManager, UserMemoryCursor};
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::{
    CurrentTask, EventHandler, Kernel, Task, WaitCallback, WaitKey, WaitQueue, Waiter, WaiterRef,
};
use crate::types::*;
use bitflags::bitflags;
use fuchsia_zircon as zx;
use slab::Slab;
use std::collections::{btree_map::Entry as BTreeMapEntry, BTreeMap, BTreeSet, VecDeque};
use std::sync::{Arc, Weak};
use zerocopy::{AsBytes, FromBytes};

/// Android's binder kernel driver implementation.
pub struct BinderDriver {
    /// The "name server" process that is addressed via the special handle 0 and is responsible
    /// for implementing the binder protocol `IServiceManager`.
    context_manager: RwLock<Option<Arc<BinderObject>>>,

    /// Manages the internal state of each process interacting with the binder driver.
    ///
    /// The Driver owns the BinderProcess. There can be at most one connection to the binder driver
    /// per process. When the last file descriptor to the binder in the process is closed, the
    /// value is removed from the map.
    procs: RwLock<BTreeMap<pid_t, Arc<BinderProcess>>>,
}

impl DeviceOps for Arc<BinderDriver> {
    fn open(
        &self,
        current_task: &CurrentTask,
        _id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let pid = current_task.get_pid();
        let binder_proc = Arc::new(BinderProcess::new(pid));
        match self.procs.write().entry(pid) {
            BTreeMapEntry::Vacant(entry) => {
                // The process has not previously opened the binder device.
                entry.insert(binder_proc);
            }
            _ => {
                // A process cannot open the same binder device more than once.
                return error!(EINVAL);
            }
        }
        Ok(Box::new(BinderConnection { pid, driver: self.clone() }))
    }
}

/// An instance of the binder driver, associated with the process that opened the binder device.
struct BinderConnection {
    /// The process that opened the binder device.
    pid: pid_t,
    /// The implementation of the binder driver.
    driver: Arc<BinderDriver>,
}

impl BinderConnection {
    fn proc(&self, task: &CurrentTask) -> Result<Arc<BinderProcess>, Errno> {
        if task.get_pid() == self.pid {
            self.driver.find_process(self.pid)
        } else {
            error!(EINVAL)
        }
    }
}

impl Drop for BinderConnection {
    fn drop(&mut self) {
        self.driver.procs.write().remove(&self.pid);
    }
}

impl FileOps for BinderConnection {
    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        FdEvents::POLLIN | FdEvents::POLLOUT
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        match self.proc(current_task) {
            Ok(proc) => {
                let binder_thread =
                    proc.thread_pool.write().find_or_register_thread(&proc, current_task.get_tid());
                self.driver.wait_async(&proc, &binder_thread, waiter, events, handler, options)
            }
            Err(_) => {
                handler(FdEvents::POLLERR);
                WaitKey::empty()
            }
        }
    }

    fn cancel_wait(&self, current_task: &CurrentTask, _waiter: &Waiter, key: WaitKey) {
        if let Ok(proc) = self.proc(current_task) {
            self.driver.cancel_wait(&proc, key)
        }
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        let proc = self.proc(current_task)?;
        let binder_thread =
            proc.thread_pool.write().find_or_register_thread(&proc, current_task.get_tid());
        self.driver.ioctl(current_task, &proc, &binder_thread, request, user_addr)
    }

    fn get_vmo(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _length: Option<usize>,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        panic!("get_vmo should never be called directly.");
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
        self.driver.mmap(
            current_task,
            &self.proc(current_task)?,
            addr,
            length,
            flags,
            mapping_options,
            filename,
        )
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
    thread_pool: RwLock<ThreadPool>,
    /// Binder objects hosted by the process shared with other processes.
    objects: Mutex<BTreeMap<UserAddress, Weak<BinderObject>>>,
    /// Handle table of remote binder objects.
    handles: Mutex<HandleTable>,
    /// A queue for commands that could not be scheduled on any existing binder threads. Binder
    /// threads that exhaust their own queue will read from this one.
    command_queue: Mutex<VecDeque<Command>>,
    /// When there are no commands in a thread's and the process' command queue, a binder thread can
    /// register with this [`WaitQueue`] to be notified when commands are available.
    waiters: Mutex<WaitQueue>,
    /// State associated with active transactions, keyed by the userspace addresses of the buffers
    /// allocated to them. When the process frees a transaction buffer with `BC_FREE_BUFFER`, the
    /// state is dropped, releasing temporary strong references and the memory allocated to the
    /// transaction.
    active_transactions: Mutex<BTreeMap<UserAddress, ActiveTransaction>>,
    /// The list of processes that should be notified if this process dies.
    death_subscribers: Mutex<Vec<(Weak<BinderProcess>, binder_uintptr_t)>>,
}

/// An active binder transaction.
#[derive(Debug)]
struct ActiveTransaction {
    /// The transaction's request type.
    request_type: RequestType,
    /// The state associated with the transaction. Not read, exists to be dropped along with the
    /// [`ActiveTransaction`] object.
    _state: TransactionState,
}

/// State held for the duration of a transaction. When a transaction completes (or fails), this
/// state is dropped, decrementing temporary strong references to binder objects.
#[derive(Debug)]
struct TransactionState {
    // The process whose handle table `handles` belong to.
    proc: Weak<BinderProcess>,
    // The handles to decrement their strong reference count.
    handles: Vec<Handle>,
}

impl Drop for TransactionState {
    fn drop(&mut self) {
        if let Some(proc) = self.proc.upgrade() {
            let mut handles = proc.handles.lock();
            for handle in &self.handles {
                // Ignore the error because there is little we can do about it.
                // Panicking would be wrong, in case the client issued an extra strong decrement.
                let _: Result<(), Errno> = handles.dec_strong(handle.object_index());
            }
        }
    }
}

/// Transaction state held during the processing and dispatching of a transaction. In the event of
/// an error while dispatching a transaction, this object is meant to cleanup any temporary
/// resources that were allocated. Once a transaction has been dispatched successfully, this object
/// can be converted into a [`TransactionState`] to be held for the lifetime of the transaction.
#[derive(Debug)]
struct TransientTransactionState<'a> {
    /// The part of the transient state that will live for the lifetime of the transaction.
    state: TransactionState,
    /// The task to which the transient file descriptors belong.
    task: &'a Task,
    /// The file descriptors to close in case of an error.
    transient_fds: Vec<FdNumber>,
}

impl<'a> TransientTransactionState<'a> {
    /// Creates a new [`TransientTransactionState`], whose resources will belong to `task` and
    /// `target_proc` for FDs and binder handles respectively.
    fn new(task: &'a Task, target_proc: &Arc<BinderProcess>) -> Self {
        TransientTransactionState {
            state: TransactionState { proc: Arc::downgrade(target_proc), handles: Vec::new() },
            task,
            transient_fds: Vec::new(),
        }
    }

    /// Schedule `handle` to have its strong reference count decremented if the transaction fails.
    /// If the transaction succeeds and this object is converted into a [`TransactionState`], the
    /// strong reference will be released when the transaction completes ([`TransactionState`] is
    /// dropped).
    fn push_handle(&mut self, handle: Handle) {
        self.state.handles.push(handle)
    }

    /// Schedule `fd` to be removed from the file descriptor table if the transaction fails.
    fn push_fd(&mut self, fd: FdNumber) {
        self.transient_fds.push(fd)
    }
}

impl<'a> Drop for TransientTransactionState<'a> {
    fn drop(&mut self) {
        for fd in &self.transient_fds {
            let _: Result<(), Errno> = self.task.files.close(*fd);
        }
    }
}

impl<'a> From<TransientTransactionState<'a>> for TransactionState {
    fn from(mut transient: TransientTransactionState<'a>) -> Self {
        // Clear the transient FD list, so that these FDs no longer get closed.
        transient.transient_fds.clear();
        // We cannot move out due to the Drop impl, so drain the handles instead and create a new
        // instance of `TransactionState`.
        let handles = transient.state.handles.drain(..).collect();
        let proc = std::mem::replace(&mut transient.state.proc, Weak::new());
        TransactionState { proc, handles }
    }
}

/// The request type of a transaction.
#[derive(Debug)]
enum RequestType {
    /// A fire-and-forget request, which has special ordering guarantees.
    Oneway {
        /// The recipient of the transaction. Oneway transactions are ordered for a given binder
        /// object.
        object: Weak<BinderObject>,
    },
    /// A request/response type.
    RequestResponse,
}

impl BinderProcess {
    fn new(pid: pid_t) -> Self {
        Self {
            pid,
            shared_memory: Mutex::new(None),
            thread_pool: RwLock::new(ThreadPool::default()),
            objects: Mutex::new(BTreeMap::new()),
            handles: Mutex::new(HandleTable::default()),
            command_queue: Mutex::new(VecDeque::new()),
            waiters: Mutex::new(WaitQueue::default()),
            active_transactions: Mutex::new(BTreeMap::new()),
            death_subscribers: Mutex::new(Vec::new()),
        }
    }

    /// Enqueues `command` for the process and wakes up any thread that is waiting for commands.
    pub fn enqueue_command(&self, command: Command) {
        self.command_queue.lock().push_back(command);
        self.waiters.lock().notify_events(FdEvents::POLLIN);
    }

    /// Finds the binder object that corresponds to the process-local addresses `local`, or creates
    /// a new [`BinderObject`] to represent the one in the process.
    pub fn find_or_register_object(
        self: &Arc<BinderProcess>,
        local: LocalBinderObject,
    ) -> Arc<BinderObject> {
        let mut objects = self.objects.lock();
        match objects.entry(local.weak_ref_addr) {
            BTreeMapEntry::Occupied(mut entry) => {
                if let Some(binder_object) = entry.get().upgrade() {
                    binder_object
                } else {
                    let binder_object = Arc::new(BinderObject::new(self, local));
                    entry.insert(Arc::downgrade(&binder_object));
                    binder_object
                }
            }
            BTreeMapEntry::Vacant(entry) => {
                let binder_object = Arc::new(BinderObject::new(self, local));
                entry.insert(Arc::downgrade(&binder_object));
                binder_object
            }
        }
    }
}

impl Drop for BinderProcess {
    fn drop(&mut self) {
        {
            // Notify any subscribers that the objects this process owned are now dead.
            let death_subscribers = self.death_subscribers.lock();
            for (proc, cookie) in &*death_subscribers {
                if let Some(target_proc) = proc.upgrade() {
                    let thread_pool = target_proc.thread_pool.read();
                    if let Some(target_thread) = thread_pool.find_available_thread() {
                        target_thread.write().enqueue_command(Command::DeadBinder(*cookie));
                    } else {
                        target_proc.enqueue_command(Command::DeadBinder(*cookie));
                    }
                }
            }
        }

        // Notify all callers that had transactions scheduled for this process that the recipient is
        // dead.
        let command_queue = self.command_queue.lock();
        for command in &*command_queue {
            if let Command::Transaction { sender, .. } = command {
                if let Some(sender_thread) = sender.thread.upgrade() {
                    sender_thread.write().enqueue_command(Command::DeadReply);
                }
            }
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
    /// The length of the shared memory mapping in bytes.
    length: usize,
    /// The next free address in our bump allocator.
    next_free_offset: usize,

    /// The offsets of all the currently active allocations, ordered in ascending order.
    ///
    /// This is used by the allocator to check whether or not a new allocation can fit, either at
    /// the `next_free_offset` or by wrapping around to the start of the vmo.
    allocations: BTreeSet<usize>,
}

/// Contains (data buffer, offsets buffer, scatter gather buffer).
type SharedMemoryAllocation<'a> =
    (SharedBuffer<'a, u8>, SharedBuffer<'a, binder_uintptr_t>, SharedBuffer<'a, u8>);

impl Drop for SharedMemory {
    fn drop(&mut self) {
        let kernel_root_vmar = fuchsia_runtime::vmar_root_self();

        // SAFETY: This object hands out references to the mapped memory, but the borrow checker
        // ensures correct lifetimes.
        let res = unsafe { kernel_root_vmar.unmap(self.kernel_address as usize, self.length) };
        match res {
            Ok(()) => {}
            Err(status) => {
                tracing::error!("failed to unmap shared binder region from kernel: {:?}", status);
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
                tracing::error!("failed to map shared binder region in kernel: {:?}", status);
                errno!(ENOMEM)
            })?;
        Ok(Self {
            kernel_address: kernel_address as *mut u8,
            user_address,
            length,
            next_free_offset: 0,
            allocations: BTreeSet::new(),
        })
    }

    /// Allocates three buffers large enough to hold the requested data, offsets, and scatter-gather
    /// buffer lengths, inserting padding between data and offsets as needed. `offsets_length` and
    /// `sg_buffers_length` must be 8-byte aligned.
    ///
    /// NOTE: When `data_length` is zero, a minimum data buffer size of 8 bytes is still allocated.
    /// This is because clients expect their buffer addresses to be uniquely associated with a
    /// transaction. Returning the same address for different transactions will break oneway
    /// transactions that have no payload.
    //
    // This is a temporary implementation of an allocator and should be replaced by something
    // more sophisticated. It currently implements a bump allocator strategy.
    fn allocate_buffers(
        &mut self,
        data_length: usize,
        offsets_length: usize,
        sg_buffers_length: usize,
    ) -> Result<SharedMemoryAllocation<'_>, Errno> {
        // Round `data_length` up to the nearest multiple of 8, so that the offsets buffer is
        // aligned when we pack it next to the data buffer.
        let data_cap = round_up_to_increment(data_length, std::mem::size_of::<binder_uintptr_t>())?;
        // Ensure that we allocate at least 8 bytes, so that each buffer returned is uniquely
        // associated with a transaction. Otherwise, multiple zero-sized allocations will have the
        // same address and there will be no way of distinguishing which transaction they belong to.
        let data_cap = std::cmp::max(data_cap, std::mem::size_of::<binder_uintptr_t>());
        // Ensure that the offsets and buffers lengths are valid.
        if offsets_length % std::mem::size_of::<binder_uintptr_t>() != 0
            || sg_buffers_length % std::mem::size_of::<binder_uintptr_t>() != 0
        {
            return error!(EINVAL);
        }
        let total_length = data_cap
            .checked_add(offsets_length)
            .and_then(|v| v.checked_add(sg_buffers_length))
            .ok_or_else(|| errno!(EINVAL))?;

        // Find the maximum offset that can result from this allocation without overwriting the next
        // allocation in the cycle, or the end of the vmo.
        let max_allocation_boundary =
            *self.allocations.range(self.next_free_offset..).next().unwrap_or(&self.length);

        let offset_after_allocation = match self.next_free_offset.checked_add(total_length) {
            Some(offset_after_allocation) if offset_after_allocation <= max_allocation_boundary => {
                // The allocation fit within the boundary, so just return the new offset.
                Ok(offset_after_allocation)
            }
            _overflowing_offset => {
                // The allocation did not fit within the boundary (or usize::MAX), so try to wrap
                // around and map it at the start.
                let min_allocation_boundary =
                    self.allocations.iter().next().copied().unwrap_or(self.length);
                if total_length <= min_allocation_boundary {
                    self.next_free_offset = 0;
                    Ok(total_length)
                } else {
                    error!(ENOMEM)
                }
            }
        }?;

        let this_offset = self.next_free_offset;
        self.allocations.insert(this_offset);
        self.next_free_offset = offset_after_allocation;

        // SAFETY: The offsets and lengths have been bounds-checked above. Constructing a
        // `SharedBuffer` should be safe.
        return unsafe {
            Ok((
                SharedBuffer::new_unchecked(self, this_offset, data_length),
                SharedBuffer::new_unchecked(self, this_offset + data_cap, offsets_length),
                SharedBuffer::new_unchecked(
                    self,
                    this_offset + data_cap + offsets_length,
                    sg_buffers_length,
                ),
            ))
        };
    }

    // This temporary allocator implementation does not reclaim free buffers.
    fn free_buffer(&mut self, buffer: UserAddress) -> Result<(), Errno> {
        // Sanity check that the buffer being freed came from this memory region.
        if buffer < self.user_address || buffer >= self.user_address + self.length {
            return error!(EINVAL);
        }
        let offset = buffer - self.user_address;
        self.allocations.remove(&offset);
        Ok(())
    }
}

/// A buffer of memory allocated from a binder process' [`SharedMemory`].
#[derive(Debug)]
struct SharedBuffer<'a, T> {
    memory: &'a SharedMemory,
    /// Offset into the shared memory region where the buffer begins.
    offset: usize,
    /// The length of the buffer in bytes.
    length: usize,
    // A zero-sized type that satisfies the compiler's need for the struct to reference `T`, which
    // is used in `as_mut_bytes` and `as_bytes`.
    _phantom_data: std::marker::PhantomData<T>,
}

impl<'a, T: AsBytes> SharedBuffer<'a, T> {
    /// Creates a new `SharedBuffer`, which represents a sub-region of `memory` starting at `offset`
    /// bytes, with `length` bytes.
    ///
    /// This is unsafe because the caller is responsible for bounds-checking the sub-region and
    /// ensuring it is not aliased.
    unsafe fn new_unchecked(memory: &'a SharedMemory, offset: usize, length: usize) -> Self {
        Self { memory, offset, length, _phantom_data: std::marker::PhantomData }
    }

    /// Returns a mutable slice of the buffer.
    fn as_mut_bytes(&mut self) -> &'a mut [T] {
        // SAFETY: `offset + length` was bounds-checked by `allocate_buffers`, and the
        // memory region pointed to was zero-allocated by mapping a new VMO.
        unsafe {
            std::slice::from_raw_parts_mut(
                self.memory.kernel_address.add(self.offset) as *mut T,
                self.length / std::mem::size_of::<T>(),
            )
        }
    }

    /// Returns an immutable slice of the buffer.
    fn as_bytes(&self) -> &'a [T] {
        // SAFETY: `offset + length` was bounds-checked by `allocate_buffers`, and the
        // memory region pointed to was zero-allocated by mapping a new VMO.
        unsafe {
            std::slice::from_raw_parts(
                self.memory.kernel_address.add(self.offset) as *const T,
                self.length / std::mem::size_of::<T>(),
            )
        }
    }

    /// The userspace address and length of the buffer.
    fn user_buffer(&self) -> UserBuffer {
        UserBuffer { address: self.memory.user_address + self.offset, length: self.length }
    }
}

/// The set of threads that are interacting with the binder driver for a given process.
#[derive(Debug, Default)]
struct ThreadPool(BTreeMap<pid_t, Arc<BinderThread>>);

impl ThreadPool {
    fn find_or_register_thread(
        &mut self,
        binder_proc: &Arc<BinderProcess>,
        tid: pid_t,
    ) -> Arc<BinderThread> {
        self.0.entry(tid).or_insert_with(|| Arc::new(BinderThread::new(binder_proc, tid))).clone()
    }

    /// Finds the first available binder thread that is registered with the driver, is not in the
    /// middle of a transaction, and has no work to do.
    fn find_available_thread(&self) -> Option<Arc<BinderThread>> {
        self.0
            .values()
            .find(|thread| {
                let thread_state = thread.read();
                thread_state
                    .registration
                    .intersects(RegistrationState::MAIN | RegistrationState::REGISTERED)
                    && thread_state.command_queue.is_empty()
                    && thread_state.waiter.is_valid()
                    && thread_state.transactions.is_empty()
            })
            .cloned()
    }
}

/// Table containing handles to remote binder objects.
#[derive(Debug, Default)]
struct HandleTable {
    table: Slab<BinderObjectRef>,
}

/// A reference to a binder object in another process.
#[derive(Debug)]
enum BinderObjectRef {
    /// A strong reference that keeps the remote binder object from being destroyed.
    StrongRef { strong_ref: Arc<BinderObject>, strong_count: usize, weak_count: usize },
    /// A weak reference that does not keep the remote binder object from being destroyed, but still
    /// occupies a place in the handle table.
    WeakRef { weak_ref: Weak<BinderObject>, weak_count: usize },
}

/// Instructs the [`HandleTable`] on what to do with a handle after the
/// [`BinderObjectRef::dec_strong`] and [`BinderObjectRef::dec_weak`] operations.
enum HandleAction {
    Keep,
    Drop,
}

impl BinderObjectRef {
    /// Increments the strong reference count of the binder object reference, promoting the
    /// reference to a strong reference if it was weak, or failing if the object no longer exists.
    fn inc_strong(&mut self) -> Result<(), Errno> {
        match self {
            BinderObjectRef::StrongRef { strong_count, .. } => *strong_count += 1,
            BinderObjectRef::WeakRef { weak_ref, weak_count } => {
                let strong_ref = weak_ref.upgrade().ok_or_else(|| errno!(EINVAL))?;
                *self = BinderObjectRef::StrongRef {
                    strong_ref,
                    strong_count: 1,
                    weak_count: *weak_count,
                };
            }
        }
        Ok(())
    }

    /// Increments the weak reference count of the binder object reference.
    fn inc_weak(&mut self) {
        match self {
            BinderObjectRef::StrongRef { weak_count, .. }
            | BinderObjectRef::WeakRef { weak_count, .. } => *weak_count += 1,
        }
    }

    /// Decrements the strong reference count of the binder object reference, demoting the reference
    /// to a weak reference or returning [`HandleAction::Drop`] if the handle should be dropped.
    fn dec_strong(&mut self) -> Result<HandleAction, Errno> {
        match self {
            BinderObjectRef::WeakRef { .. } => return error!(EINVAL),
            BinderObjectRef::StrongRef { strong_ref, strong_count, weak_count } => {
                *strong_count -= 1;
                if *strong_count == 0 {
                    let weak_count = *weak_count;
                    *self = BinderObjectRef::WeakRef {
                        weak_ref: Arc::downgrade(strong_ref),
                        weak_count,
                    };
                    if weak_count == 0 {
                        return Ok(HandleAction::Drop);
                    }
                }
            }
        }
        Ok(HandleAction::Keep)
    }

    /// Decrements the weak reference count of the binder object reference, returning
    /// [`HandleAction::Drop`] if the strong count and weak count both drop to 0.
    fn dec_weak(&mut self) -> Result<HandleAction, Errno> {
        match self {
            BinderObjectRef::StrongRef { weak_count, .. } => {
                if *weak_count == 0 {
                    error!(EINVAL)
                } else {
                    *weak_count -= 1;
                    Ok(HandleAction::Keep)
                }
            }
            BinderObjectRef::WeakRef { weak_count, .. } => {
                *weak_count -= 1;
                if *weak_count == 0 {
                    Ok(HandleAction::Drop)
                } else {
                    Ok(HandleAction::Keep)
                }
            }
        }
    }
}

impl HandleTable {
    /// Inserts a reference to a binder object, returning a handle that represents it. The handle
    /// may be an existing handle if the object was already present in the table. If the client does
    /// not acquire a strong reference to this handle before the transaction that inserted it is
    /// complete, the handle will be dropped.
    fn insert_for_transaction(&mut self, object: Arc<BinderObject>) -> Handle {
        let existing_idx = self.table.iter().find_map(|(idx, object_ref)| match object_ref {
            BinderObjectRef::WeakRef { weak_ref, .. } => {
                weak_ref.upgrade().and_then(|strong_ref| {
                    (strong_ref.local.weak_ref_addr == object.local.weak_ref_addr).then_some(idx)
                })
            }
            BinderObjectRef::StrongRef { strong_ref, .. } => {
                (strong_ref.local.weak_ref_addr == object.local.weak_ref_addr).then_some(idx)
            }
        });

        if let Some(existing_idx) = existing_idx {
            // Increment the number of strong reference, as the caller expects having a strong
            // reference to it.
            // The incrementation cannot fail as the index has just been computed and checked.
            self.inc_strong(existing_idx).expect("inc_strong");
            return Handle::Object { index: existing_idx };
        }

        let new_idx = self.table.insert(BinderObjectRef::StrongRef {
            strong_ref: object,
            strong_count: 1,
            weak_count: 0,
        });
        Handle::Object { index: new_idx }
    }

    /// Retrieves a reference to a binder object at index `idx`.
    fn get(&self, idx: usize) -> Option<Arc<BinderObject>> {
        let object_ref = self.table.get(idx)?;
        match object_ref {
            BinderObjectRef::WeakRef { weak_ref, .. } => weak_ref.upgrade(),
            BinderObjectRef::StrongRef { strong_ref, .. } => Some(strong_ref.clone()),
        }
    }

    /// Increments the strong reference count of the binder object reference at index `idx`,
    /// failing if the object no longer exists.
    fn inc_strong(&mut self, idx: usize) -> Result<(), Errno> {
        self.table.get_mut(idx).ok_or_else(|| errno!(ENOENT))?.inc_strong()
    }

    /// Increments the weak reference count of the binder object reference at index `idx`, failing
    /// if the object does not exist.
    fn inc_weak(&mut self, idx: usize) -> Result<(), Errno> {
        self.table.get_mut(idx).ok_or_else(|| errno!(ENOENT))?.inc_weak();
        Ok(())
    }

    /// Decrements the strong reference count of the binder object reference at index `idx`, failing
    /// if the object no longer exists.
    fn dec_strong(&mut self, idx: usize) -> Result<(), Errno> {
        let object_ref = self.table.get_mut(idx).ok_or_else(|| errno!(ENOENT))?;
        if let HandleAction::Drop = object_ref.dec_strong()? {
            self.table.remove(idx);
        }
        Ok(())
    }

    /// Decrements the weak reference count of the binder object reference at index `idx`, failing
    /// if the object does not exist.
    fn dec_weak(&mut self, idx: usize) -> Result<(), Errno> {
        let object_ref = self.table.get_mut(idx).ok_or_else(|| errno!(ENOENT))?;
        if let HandleAction::Drop = object_ref.dec_weak()? {
            self.table.remove(idx);
        }
        Ok(())
    }
}

#[derive(Debug)]
struct BinderThread {
    tid: pid_t,
    /// The mutable state of the binder thread, protected by a single lock.
    state: RwLock<BinderThreadState>,
}

impl BinderThread {
    fn new(binder_proc: &Arc<BinderProcess>, tid: pid_t) -> Self {
        Self { tid, state: RwLock::new(BinderThreadState::new(binder_proc)) }
    }

    /// Acquire a reader lock to the binder thread's mutable state.
    pub fn read(&self) -> RwLockReadGuard<'_, BinderThreadState> {
        self.state.read()
    }

    /// Acquire a writer lock to the binder thread's mutable state.
    pub fn write(&self) -> RwLockWriteGuard<'_, BinderThreadState> {
        self.state.write()
    }
}

/// The mutable state of a binder thread.
#[derive(Debug)]
struct BinderThreadState {
    /// The process this thread belongs to.
    process: Weak<BinderProcess>,
    /// The registered state of the thread.
    registration: RegistrationState,
    /// The stack of transactions that are active for this thread.
    transactions: Vec<TransactionRole>,
    /// The binder driver uses this queue to communicate with a binder thread. When a binder thread
    /// issues a [`uapi::BINDER_WRITE_READ`] ioctl, it will read from this command queue.
    command_queue: VecDeque<Command>,
    /// The [`Waiter`] object the binder thread is waiting on when there are no commands in the
    /// command queue. If empty, the binder thread is not currently waiting.
    waiter: WaiterRef,
}

impl BinderThreadState {
    fn new(binder_proc: &Arc<BinderProcess>) -> Self {
        Self {
            process: Arc::downgrade(binder_proc),
            registration: RegistrationState::empty(),
            transactions: Vec::new(),
            command_queue: VecDeque::new(),
            waiter: WaiterRef::empty(),
        }
    }

    /// Enqueues `command` for the thread and wakes it up if necessary.
    pub fn enqueue_command(&mut self, command: Command) {
        self.command_queue.push_back(command);
        if let Some(waiter) = self.waiter.take() {
            // Wake up the thread that is waiting.
            waiter.wake_immediately(FdEvents::POLLIN.mask(), WaitCallback::none());
        }
        // Notify any threads that are waiting on events from the binder driver FD.
        if let Some(binder_proc) = self.process.upgrade() {
            binder_proc.waiters.lock().notify_events(FdEvents::POLLIN);
        }
    }

    /// Get the binder process and thread to reply to, or fail if there is no ongoing transaction or
    /// the calling process/thread are dead.
    pub fn transaction_caller(
        &self,
    ) -> Result<(Arc<BinderProcess>, Arc<BinderThread>), TransactionError> {
        let transaction = self.transactions.last().ok_or_else(|| errno!(EINVAL))?;
        match transaction {
            TransactionRole::Receiver(peer) => peer.upgrade().ok_or(TransactionError::Dead),
            TransactionRole::Sender(_) => error!(EINVAL)?,
        }
    }
}

impl Drop for BinderThreadState {
    fn drop(&mut self) {
        // If there are any transactions queued, we need to tell the caller that this thread is now
        // dead.
        for command in &self.command_queue {
            if let Command::Transaction { sender, .. } = command {
                if let Some(sender_thread) = sender.thread.upgrade() {
                    sender_thread.write().enqueue_command(Command::DeadReply);
                }
            }
        }

        // If there are any transactions that this thread was processing, we need to tell the caller
        // that this thread is now dead and to not expect a reply.
        for transaction in &self.transactions {
            if let TransactionRole::Receiver(peer) = transaction {
                if let Some(peer_thread) = peer.thread.upgrade() {
                    peer_thread.write().enqueue_command(Command::DeadReply);
                }
            }
        }
    }
}

bitflags! {
    /// The registration state of a thread.
    struct RegistrationState: u32 {
        /// The thread is the main binder thread.
        const MAIN = 1;

        /// The thread is an auxiliary binder thread.
        const REGISTERED = 1 << 2;
    }
}

/// A pair of weak references to the process and thread of a binder transaction peer.
#[derive(Debug)]
struct WeakBinderPeer {
    proc: Weak<BinderProcess>,
    thread: Weak<BinderThread>,
}

impl WeakBinderPeer {
    fn new(proc: &Arc<BinderProcess>, thread: &Arc<BinderThread>) -> Self {
        Self { proc: Arc::downgrade(proc), thread: Arc::downgrade(thread) }
    }

    /// Upgrades the process and thread weak references as a tuple.
    fn upgrade(&self) -> Option<(Arc<BinderProcess>, Arc<BinderThread>)> {
        self.proc.upgrade().zip(self.thread.upgrade())
    }
}

/// Commands for a binder thread to execute.
#[derive(Debug)]
enum Command {
    /// Notifies a binder thread that a remote process acquired a strong reference to the specified
    /// binder object. The object should not be destroyed until a [`Command::ReleaseRef`] is
    /// delivered.
    AcquireRef(LocalBinderObject),
    /// Notifies a binder thread that there are no longer any remote processes holding strong
    /// references to the specified binder object. The object may still have references within the
    /// owning process.
    ReleaseRef(LocalBinderObject),
    /// Notifies a binder thread that the last processed command contained an error.
    Error(i32),
    /// Commands a binder thread to start processing an incoming oneway transaction, which requires
    /// no reply.
    OnewayTransaction(TransactionData),
    /// Commands a binder thread to start processing an incoming synchronous transaction from
    /// another binder process.
    Transaction {
        /// The binder peer that sent this transaction.
        sender: WeakBinderPeer,
        /// The transaction payload.
        data: TransactionData,
    },
    /// Commands a binder thread to process an incoming reply to its transaction.
    Reply(TransactionData),
    /// Notifies a binder thread that a transaction has completed.
    TransactionComplete,
    /// Notifies a binder thread that a oneway transaction has been sent.
    OnewayTransactionComplete,
    /// The transaction was well formed but failed. Possible causes are a nonexistent handle, no
    /// more memory available to allocate a buffer.
    FailedReply,
    /// Notifies the initiator of a transaction that the recipient is dead.
    DeadReply,
    /// Notifies a binder process that a binder object has died.
    DeadBinder(binder_uintptr_t),
}

impl Command {
    /// Returns the command's BR_* code for serialization.
    fn driver_return_code(&self) -> binder_driver_return_protocol {
        match self {
            Self::AcquireRef(..) => binder_driver_return_protocol_BR_ACQUIRE,
            Self::ReleaseRef(..) => binder_driver_return_protocol_BR_RELEASE,
            Self::Error(..) => binder_driver_return_protocol_BR_ERROR,
            Self::OnewayTransaction(..) | Self::Transaction { .. } => {
                binder_driver_return_protocol_BR_TRANSACTION
            }
            Self::Reply(..) => binder_driver_return_protocol_BR_REPLY,
            Self::TransactionComplete | Self::OnewayTransactionComplete => {
                binder_driver_return_protocol_BR_TRANSACTION_COMPLETE
            }
            Self::FailedReply => binder_driver_return_protocol_BR_FAILED_REPLY,
            Self::DeadReply => binder_driver_return_protocol_BR_DEAD_REPLY,
            Self::DeadBinder(..) => binder_driver_return_protocol_BR_DEAD_BINDER,
        }
    }

    /// Serializes and writes the command into userspace memory at `buffer`.
    fn write_to_memory(&self, mm: &MemoryManager, buffer: &UserBuffer) -> Result<usize, Errno> {
        match self {
            Self::AcquireRef(obj) | Self::ReleaseRef(obj) => {
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
            Self::Error(error_val) => {
                #[repr(C, packed)]
                #[derive(AsBytes)]
                struct ErrorData {
                    command: binder_driver_return_protocol,
                    error_val: i32,
                }
                if buffer.length < std::mem::size_of::<ErrorData>() {
                    return error!(ENOMEM);
                }
                mm.write_object(
                    UserRef::new(buffer.address),
                    &ErrorData { command: self.driver_return_code(), error_val: *error_val },
                )
            }
            Self::OnewayTransaction(data) | Self::Transaction { data, .. } | Self::Reply(data) => {
                #[repr(C, packed)]
                #[derive(AsBytes)]
                struct TransactionData {
                    command: binder_driver_return_protocol,
                    data: [u8; std::mem::size_of::<binder_transaction_data>()],
                }
                if buffer.length < std::mem::size_of::<TransactionData>() {
                    return error!(ENOMEM);
                }
                mm.write_object(
                    UserRef::new(buffer.address),
                    &TransactionData { command: self.driver_return_code(), data: data.as_bytes() },
                )
            }
            Self::TransactionComplete
            | Self::OnewayTransactionComplete
            | Self::FailedReply
            | Self::DeadReply => {
                if buffer.length < std::mem::size_of::<binder_driver_return_protocol>() {
                    return error!(ENOMEM);
                }
                mm.write_object(UserRef::new(buffer.address), &self.driver_return_code())
            }
            Self::DeadBinder(cookie) => {
                #[repr(C, packed)]
                #[derive(AsBytes)]
                struct DeadBinderData {
                    command: binder_driver_return_protocol,
                    cookie: binder_uintptr_t,
                }
                if buffer.length < std::mem::size_of::<DeadBinderData>() {
                    return error!(ENOMEM);
                }
                mm.write_object(
                    UserRef::new(buffer.address),
                    &DeadBinderData { command: self.driver_return_code(), cookie: *cookie },
                )
            }
        }
    }
}

/// A binder object, which is owned by a process. Process-local unique memory addresses identify it
/// to the owner.
#[derive(Debug)]
struct BinderObject {
    /// The owner of the binder object. If the owner cannot be promoted to a strong reference,
    /// the object is dead.
    owner: Weak<BinderProcess>,
    /// The addresses to the binder (weak and strong) in the owner's address space. These are
    /// treated as opaque identifiers in the driver, and only have meaning to the owning process.
    local: LocalBinderObject,
    /// Mutable state for the binder object, protected behind a mutex.
    state: Mutex<BinderObjectMutableState>,
}

/// Mutable state of a [`BinderObject`], mainly for handling the ordering guarantees of oneway
/// transactions.
#[derive(Debug)]
struct BinderObjectMutableState {
    /// Command queue for oneway transactions on this binder object. Oneway transactions are
    /// guaranteed to be dispatched in the order they are submitted to the driver, and one at a
    /// time.
    oneway_transactions: VecDeque<TransactionData>,
    /// Whether a binder thread is currently handling a oneway transaction. This will get cleared
    /// when there are no more transactions in the `oneway_transactions` and a binder thread freed
    /// the buffer associated with the last oneway transaction.
    handling_oneway_transaction: bool,
}

impl BinderObject {
    fn new(owner: &Arc<BinderProcess>, local: LocalBinderObject) -> Self {
        Self {
            owner: Arc::downgrade(owner),
            local,
            state: Mutex::new(BinderObjectMutableState {
                oneway_transactions: VecDeque::new(),
                handling_oneway_transaction: false,
            }),
        }
    }

    /// Locks the mutable state of the binder object for exclusive access.
    fn lock(&self) -> MutexGuard<'_, BinderObjectMutableState> {
        self.state.lock()
    }
}

impl Drop for BinderObject {
    fn drop(&mut self) {
        if let Some(owner) = self.owner.upgrade() {
            // The owner process is not dead, so tell it that the last remote reference has been
            // released.
            let thread_pool = owner.thread_pool.read();
            if let Some(binder_thread) = thread_pool.find_available_thread() {
                binder_thread.write().enqueue_command(Command::ReleaseRef(self.local));
            } else {
                owner.enqueue_command(Command::ReleaseRef(self.local));
            }
        }
    }
}

/// A binder object.
/// All addresses are in the owning process' address space.
#[derive(Debug, Default, Clone, Copy, Eq, PartialEq)]
struct LocalBinderObject {
    /// Address to the weak ref-count structure. This uniquely identifies a binder object within
    /// a process. Guaranteed to exist.
    weak_ref_addr: UserAddress,
    /// Address to the strong ref-count structure (actual object). May not exist if the object was
    /// destroyed.
    strong_ref_addr: UserAddress,
}

/// A binder thread's role (sender or receiver) in a synchronous transaction. Oneway transactions
/// do not record roles, since they end as soon as they begin.
#[derive(Debug)]
enum TransactionRole {
    /// The binder thread initiated the transaction and is awaiting a reply from a peer.
    Sender(WeakBinderPeer),
    /// The binder thread is receiving a transaction and is expected to reply to the peer binder
    /// process and thread.
    Receiver(WeakBinderPeer),
}

/// Non-union version of [`binder_transaction_data`].
#[derive(Debug, PartialEq, Eq)]
struct TransactionData {
    peer_pid: pid_t,
    peer_tid: pid_t,
    peer_euid: u32,

    object: FlatBinderObject,
    code: u32,
    flags: u32,

    data_buffer: UserBuffer,
    offsets_buffer: UserBuffer,
}

impl TransactionData {
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
#[derive(Debug, PartialEq, Eq)]
enum FlatBinderObject {
    Local { object: LocalBinderObject },
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

impl Handle {
    pub const fn from_raw(handle: u32) -> Handle {
        if handle == 0 {
            Handle::SpecialServiceManager
        } else {
            Handle::Object { index: handle as usize - 1 }
        }
    }

    /// Returns the underlying object index the handle represents, panicking if the handle was the
    /// special `0` handle.
    pub fn object_index(&self) -> usize {
        match self {
            Handle::SpecialServiceManager => {
                panic!("handle does not have an object index")
            }
            Handle::Object { index } => *index,
        }
    }

    pub fn is_handle_0(&self) -> bool {
        match self {
            Handle::SpecialServiceManager => true,
            Handle::Object { .. } => false,
        }
    }
}

impl From<u32> for Handle {
    fn from(handle: u32) -> Self {
        Handle::from_raw(handle)
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

impl std::fmt::Display for Handle {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Handle::SpecialServiceManager => f.write_str("0"),
            Handle::Object { index } => f.write_fmt(format_args!("{}", index + 1)),
        }
    }
}

impl BinderDriver {
    fn new() -> Arc<Self> {
        Arc::new(Self { context_manager: RwLock::new(None), procs: RwLock::new(BTreeMap::new()) })
    }

    fn find_process(&self, pid: pid_t) -> Result<Arc<BinderProcess>, Errno> {
        self.procs.read().get(&pid).map(Arc::clone).ok_or_else(|| errno!(ENOENT))
    }

    /// Creates the binder process state to represent a process with `pid`.
    #[cfg(test)]
    fn create_process(&self, pid: pid_t) -> Arc<BinderProcess> {
        let binder_process = Arc::new(BinderProcess::new(pid));
        assert!(
            self.procs.write().insert(pid, binder_process.clone()).is_none(),
            "process with same pid created"
        );
        binder_process
    }

    /// Creates the binder process and thread state to represent a process with `pid` and one main
    /// thread.
    #[cfg(test)]
    fn create_process_and_thread(&self, pid: pid_t) -> (Arc<BinderProcess>, Arc<BinderThread>) {
        let binder_process = Arc::new(BinderProcess::new(pid));
        assert!(
            self.procs.write().insert(pid, binder_process.clone()).is_none(),
            "process with same pid created"
        );
        let binder_thread =
            binder_process.thread_pool.write().find_or_register_thread(&binder_process, pid);
        (binder_process, binder_thread)
    }

    fn get_context_manager(&self) -> Result<(Arc<BinderObject>, Arc<BinderProcess>), Errno> {
        let context_manager =
            self.context_manager.read().as_ref().cloned().ok_or_else(|| errno!(ENOENT))?;
        let proc = context_manager.owner.upgrade().ok_or_else(|| errno!(ENOENT))?;
        Ok((context_manager, proc))
    }

    fn ioctl(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        request: u32,
        user_arg: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            uapi::BINDER_VERSION => {
                // A thread is requesting the version of this binder driver.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }
                let response =
                    binder_version { protocol_version: BINDER_CURRENT_PROTOCOL_VERSION as i32 };
                current_task.mm.write_object(UserRef::new(user_arg), &response)?;
                Ok(SUCCESS)
            }
            uapi::BINDER_SET_CONTEXT_MGR | uapi::BINDER_SET_CONTEXT_MGR_EXT => {
                // A process is registering itself as the context manager.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }

                // TODO: Read the flat_binder_object when ioctl is uapi::BINDER_SET_CONTEXT_MGR_EXT.

                *self.context_manager.write() =
                    Some(Arc::new(BinderObject::new(binder_proc, LocalBinderObject::default())));
                Ok(SUCCESS)
            }
            uapi::BINDER_WRITE_READ => {
                // A thread is requesting to exchange data with the binder driver.

                if user_arg.is_null() {
                    return error!(EINVAL);
                }

                let user_ref = UserRef::<binder_write_read>::new(user_arg);
                let mut input = current_task.mm.read_object(user_ref)?;

                // We will be writing this back to userspace, don't trust what the client gave us.
                input.write_consumed = 0;
                input.read_consumed = 0;

                if input.write_size > 0 {
                    // The calling thread wants to write some data to the binder driver.
                    let mut cursor = UserMemoryCursor::new(
                        &current_task.mm,
                        UserAddress::from(input.write_buffer),
                        input.write_size,
                    );

                    // Handle all the data the calling thread sent, which may include multiple
                    // commands.
                    while cursor.bytes_read() < input.write_size as usize {
                        self.handle_thread_write(
                            current_task,
                            binder_proc,
                            binder_thread,
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
                    input.read_consumed = self.handle_thread_read(
                        current_task,
                        binder_proc,
                        binder_thread,
                        &read_buffer,
                    )? as u64;
                }

                // Write back to the calling thread how much data was read/written.
                current_task.mm.write_object(user_ref, &input)?;
                Ok(SUCCESS)
            }
            uapi::BINDER_SET_MAX_THREADS => {
                not_implemented!(current_task, "binder ignoring SET_MAX_THREADS ioctl");
                Ok(SUCCESS)
            }
            uapi::BINDER_ENABLE_ONEWAY_SPAM_DETECTION => {
                not_implemented!(
                    current_task,
                    "binder ignoring ENABLE_ONEWAY_SPAM_DETECTION ioctl"
                );
                Ok(SUCCESS)
            }
            uapi::BINDER_THREAD_EXIT => {
                not_implemented!(current_task, "binder ignoring THREAD_EXIT ioctl");
                Ok(SUCCESS)
            }
            uapi::BINDER_GET_NODE_DEBUG_INFO => {
                not_implemented!(current_task, "binder GET_NODE_DEBUG_INFO ioctl not supported");
                error!(EOPNOTSUPP)
            }
            uapi::BINDER_GET_NODE_INFO_FOR_REF => {
                not_implemented!(current_task, "binder GET_NODE_INFO_FOR_REF ioctl not supported");
                error!(EOPNOTSUPP)
            }
            _ => {
                tracing::error!("binder received unknown ioctl request 0x{:08x}", request);
                error!(EINVAL)
            }
        }
    }

    /// Consumes one command from the userspace binder_write_read buffer and handles it.
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
            binder_driver_command_protocol_BC_ENTER_LOOPER => {
                self.handle_looper_registration(binder_thread, RegistrationState::MAIN)
            }
            binder_driver_command_protocol_BC_REGISTER_LOOPER => {
                self.handle_looper_registration(binder_thread, RegistrationState::REGISTERED)
            }
            binder_driver_command_protocol_BC_INCREFS
            | binder_driver_command_protocol_BC_ACQUIRE
            | binder_driver_command_protocol_BC_DECREFS
            | binder_driver_command_protocol_BC_RELEASE => {
                let handle = cursor.read_object::<u32>()?.into();
                self.handle_refcount_operation(current_task, command, binder_proc, handle)
            }
            binder_driver_command_protocol_BC_INCREFS_DONE
            | binder_driver_command_protocol_BC_ACQUIRE_DONE => {
                let object = LocalBinderObject {
                    weak_ref_addr: UserAddress::from(cursor.read_object::<binder_uintptr_t>()?),
                    strong_ref_addr: UserAddress::from(cursor.read_object::<binder_uintptr_t>()?),
                };
                self.handle_refcount_operation_done(current_task, command, binder_thread, object)
            }
            binder_driver_command_protocol_BC_FREE_BUFFER => {
                let buffer_ptr = UserAddress::from(cursor.read_object::<binder_uintptr_t>()?);
                self.handle_free_buffer(binder_proc, buffer_ptr)
            }
            binder_driver_command_protocol_BC_REQUEST_DEATH_NOTIFICATION => {
                let handle = cursor.read_object::<u32>()?.into();
                let cookie = cursor.read_object::<binder_uintptr_t>()?;
                self.handle_request_death_notification(
                    current_task,
                    binder_proc,
                    binder_thread,
                    handle,
                    cookie,
                )
            }
            binder_driver_command_protocol_BC_CLEAR_DEATH_NOTIFICATION => {
                let handle = cursor.read_object::<u32>()?.into();
                let cookie = cursor.read_object::<binder_uintptr_t>()?;
                self.handle_clear_death_notification(current_task, binder_proc, handle, cookie)
            }
            binder_driver_command_protocol_BC_DEAD_BINDER_DONE => {
                let _cookie = cursor.read_object::<binder_uintptr_t>()?;
                Ok(())
            }
            binder_driver_command_protocol_BC_TRANSACTION => {
                let data = cursor.read_object::<binder_transaction_data>()?;
                self.handle_transaction(
                    current_task,
                    binder_proc,
                    binder_thread,
                    binder_transaction_data_sg { transaction_data: data, buffers_size: 0 },
                )
                .or_else(|err| err.dispatch(binder_thread))
            }
            binder_driver_command_protocol_BC_REPLY => {
                let data = cursor.read_object::<binder_transaction_data>()?;
                self.handle_reply(
                    current_task,
                    binder_proc,
                    binder_thread,
                    binder_transaction_data_sg { transaction_data: data, buffers_size: 0 },
                )
                .or_else(|err| err.dispatch(binder_thread))
            }
            binder_driver_command_protocol_BC_TRANSACTION_SG => {
                let data = cursor.read_object::<binder_transaction_data_sg>()?;
                self.handle_transaction(current_task, binder_proc, binder_thread, data)
                    .or_else(|err| err.dispatch(binder_thread))
            }
            binder_driver_command_protocol_BC_REPLY_SG => {
                let data = cursor.read_object::<binder_transaction_data_sg>()?;
                self.handle_reply(current_task, binder_proc, binder_thread, data)
                    .or_else(|err| err.dispatch(binder_thread))
            }
            _ => {
                tracing::error!("binder received unknown RW command: 0x{:08x}", command);
                error!(EINVAL)
            }
        }
    }

    /// Handle a binder thread's request to register itself with the binder driver.
    /// This makes the binder thread eligible for receiving commands from the driver.
    fn handle_looper_registration(
        &self,
        binder_thread: &Arc<BinderThread>,
        registration: RegistrationState,
    ) -> Result<(), Errno> {
        let mut thread_state = binder_thread.write();
        if thread_state
            .registration
            .intersects(RegistrationState::MAIN | RegistrationState::REGISTERED)
        {
            // This thread is already registered.
            error!(EINVAL)
        } else {
            thread_state.registration |= registration;
            Ok(())
        }
    }

    /// Handle a binder thread's request to increment/decrement a strong/weak reference to a remote
    /// binder object.
    fn handle_refcount_operation(
        &self,
        current_task: &CurrentTask,
        command: binder_driver_command_protocol,
        binder_proc: &Arc<BinderProcess>,
        handle: Handle,
    ) -> Result<(), Errno> {
        let idx = match handle {
            Handle::SpecialServiceManager => {
                // TODO: Figure out how to acquire/release refs for the context manager
                // object.
                not_implemented!(current_task, "acquire/release refs for context manager object");
                return Ok(());
            }
            Handle::Object { index } => index,
        };

        let mut handles = binder_proc.handles.lock();
        match command {
            binder_driver_command_protocol_BC_ACQUIRE => handles.inc_strong(idx),
            binder_driver_command_protocol_BC_RELEASE => handles.dec_strong(idx),
            binder_driver_command_protocol_BC_INCREFS => handles.inc_weak(idx),
            binder_driver_command_protocol_BC_DECREFS => handles.dec_weak(idx),
            _ => unreachable!(),
        }
    }

    /// Handle a binder thread's notification that it successfully incremented a strong/weak
    /// reference to a local (in-process) binder object. This is in response to a
    /// `BR_ACQUIRE`/`BR_INCREFS` command.
    fn handle_refcount_operation_done(
        &self,
        current_task: &CurrentTask,
        command: binder_driver_command_protocol,
        binder_thread: &Arc<BinderThread>,
        object: LocalBinderObject,
    ) -> Result<(), Errno> {
        // TODO: When the binder driver keeps track of references internally, this should
        // reduce the temporary refcount that is held while the binder thread performs the
        // refcount.
        let msg = match command {
            binder_driver_command_protocol_BC_INCREFS_DONE => "BC_INCREFS_DONE",
            binder_driver_command_protocol_BC_ACQUIRE_DONE => "BC_ACQUIRE_DONE",
            _ => unreachable!(),
        };
        not_implemented_log_once!(
            current_task,
            "binder thread {} {} {:?}",
            binder_thread.tid,
            msg,
            &object
        );
        Ok(())
    }

    /// A binder thread is done reading a buffer allocated to a transaction. The binder
    /// driver can reclaim this buffer.
    fn handle_free_buffer(
        &self,
        binder_proc: &Arc<BinderProcess>,
        buffer_ptr: UserAddress,
    ) -> Result<(), Errno> {
        // Drop the state associated with the now completed transaction.
        let active_transaction = binder_proc.active_transactions.lock().remove(&buffer_ptr);

        // Check if this was a oneway transaction and schedule the next oneway if this is the case.
        if let Some(ActiveTransaction { request_type: RequestType::Oneway { object }, .. }) =
            active_transaction
        {
            if let Some(object) = object.upgrade() {
                let mut object_state = object.lock();
                assert!(
                    object_state.handling_oneway_transaction,
                    "freeing a oneway buffer implies that a oneway transaction was being handled"
                );
                if let Some(transaction) = object_state.oneway_transactions.pop_front() {
                    // Drop the lock, as we've completed all mutations and don't want to hold this
                    // lock while acquiring any others.
                    drop(object_state);

                    // Schedule the transaction
                    // Acquire an exclusive lock to prevent a thread from being scheduled twice.
                    let target_thread_pool = binder_proc.thread_pool.write();

                    // Find a thread to handle the transaction, or use the process' command queue.
                    if let Some(target_thread) = target_thread_pool.find_available_thread() {
                        target_thread
                            .write()
                            .enqueue_command(Command::OnewayTransaction(transaction));
                    } else {
                        binder_proc.enqueue_command(Command::OnewayTransaction(transaction));
                    }
                } else {
                    // No more oneway transactions queued, mark the queue handling as done.
                    object_state.handling_oneway_transaction = false;
                }
            }
        }

        // Reclaim the memory.
        let mut shared_memory_lock = binder_proc.shared_memory.lock();
        let shared_memory = shared_memory_lock.as_mut().ok_or_else(|| errno!(ENOMEM))?;
        shared_memory.free_buffer(buffer_ptr)
    }

    /// Subscribe a process to the death of the owner of `handle`.
    fn handle_request_death_notification(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        handle: Handle,
        cookie: binder_uintptr_t,
    ) -> Result<(), Errno> {
        let proxy = match handle {
            Handle::SpecialServiceManager => {
                not_implemented!(current_task, "death notification for service manager");
                return Ok(());
            }
            Handle::Object { index } => {
                binder_proc.handles.lock().get(index).ok_or_else(|| errno!(ENOENT))?
            }
        };
        if let Some(owner) = proxy.owner.upgrade() {
            owner.death_subscribers.lock().push((Arc::downgrade(binder_proc), cookie));
        } else {
            // The object is already dead. Notify immediately. However, the requesting thread
            // cannot handle the notification, in case it is holding some mutex while processing a
            // oneway transaction (where its transaction stack will be empty).
            let thread_pool = binder_proc.thread_pool.write();
            if let Some(target_thread) =
                thread_pool.find_available_thread().filter(|th| th.tid != binder_thread.tid)
            {
                target_thread.write().enqueue_command(Command::DeadBinder(cookie));
            } else {
                binder_proc.enqueue_command(Command::DeadBinder(cookie));
            }
        }
        Ok(())
    }

    /// Remove a previously subscribed death notification.
    fn handle_clear_death_notification(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        handle: Handle,
        cookie: binder_uintptr_t,
    ) -> Result<(), Errno> {
        let proxy = match handle {
            Handle::SpecialServiceManager => {
                not_implemented!(current_task, "clear death notification for service manager");
                return Ok(());
            }
            Handle::Object { index } => {
                binder_proc.handles.lock().get(index).ok_or_else(|| errno!(ENOENT))?
            }
        };
        if let Some(owner) = proxy.owner.upgrade() {
            let mut death_subscribers = owner.death_subscribers.lock();
            if let Some((idx, _)) =
                death_subscribers.iter().enumerate().find(|(_idx, (proc, c))| {
                    std::ptr::eq(proc.as_ptr(), Arc::as_ptr(binder_proc)) && *c == cookie
                })
            {
                death_subscribers.swap_remove(idx);
            }
        }
        Ok(())
    }

    /// A binder thread is starting a transaction on a remote binder object.
    fn handle_transaction(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        data: binder_transaction_data_sg,
    ) -> Result<(), TransactionError> {
        // SAFETY: Transactions can only refer to handles.
        let handle = unsafe { data.transaction_data.target.handle }.into();

        let (object, target_proc) = match handle {
            Handle::SpecialServiceManager => self.get_context_manager()?,
            Handle::Object { index } => {
                let object =
                    binder_proc.handles.lock().get(index).ok_or(TransactionError::Failure)?;
                let owner = object.owner.upgrade().ok_or(TransactionError::Dead)?;
                (object, owner)
            }
        };

        let target_task = current_task
            .kernel()
            .pids
            .read()
            .get_task(target_proc.pid)
            .ok_or_else(|| errno!(EINVAL))?;

        // Copy the transaction data to the target process.
        let (data_buffer, offsets_buffer, transaction_state) = self.copy_transaction_buffers(
            current_task,
            binder_proc,
            binder_thread,
            &target_task,
            &target_proc,
            &data,
        )?;

        let transaction = TransactionData {
            peer_pid: binder_proc.pid,
            peer_tid: binder_thread.tid,
            peer_euid: current_task.creds().euid,
            object: {
                if handle.is_handle_0() {
                    // This handle (0) always refers to the context manager, which is always
                    // "remote", even for the context manager itself.
                    FlatBinderObject::Remote { handle }
                } else {
                    FlatBinderObject::Local { object: object.local }
                }
            },
            code: data.transaction_data.code,
            flags: data.transaction_data.flags,

            data_buffer,
            offsets_buffer,
        };

        let caller_thread = match match binder_thread.read().transactions.last() {
            Some(TransactionRole::Receiver(rx)) => rx.upgrade(),
            _ => None,
        } {
            Some((proc, thread)) if proc.pid == target_proc.pid => Some(thread),
            _ => None,
        };

        let command = if data.transaction_data.flags & transaction_flags_TF_ONE_WAY != 0 {
            // The caller is not expecting a reply.
            binder_thread.write().enqueue_command(Command::OnewayTransactionComplete);

            // Register the transaction buffer.
            target_proc.active_transactions.lock().insert(
                data_buffer.address,
                ActiveTransaction {
                    request_type: RequestType::Oneway { object: Arc::downgrade(&object) },
                    _state: transaction_state.into(),
                },
            );

            // Oneway transactions are enqueued on the binder object and processed one at a time.
            // This guarantees that oneway transactions are processed in the order they are
            // submitted, and one at a time.
            let mut object_state = object.lock();
            if object_state.handling_oneway_transaction {
                // Currently, a oneway transaction is being handled. Queue this one so that it is
                // scheduled when the buffer from the in-progress transaction is freed.
                object_state.oneway_transactions.push_back(transaction);
                return Ok(());
            }

            // No oneway transactions are being handled, which means that no buffer will be
            // freed, kicking off scheduling from the oneway queue. Instead, we must schedule
            // the transaction regularly, but mark the object as handling a oneway transaction.
            object_state.handling_oneway_transaction = true;

            Command::OnewayTransaction(transaction)
        } else {
            // Make the sender thread part of the transaction so it doesn't get scheduled to handle
            // any other transactions.
            binder_thread
                .write()
                .transactions
                .push(TransactionRole::Sender(WeakBinderPeer::new(binder_proc, binder_thread)));

            // Register the transaction buffer.
            target_proc.active_transactions.lock().insert(
                data_buffer.address,
                ActiveTransaction {
                    request_type: RequestType::RequestResponse,
                    _state: transaction_state.into(),
                },
            );

            Command::Transaction {
                sender: WeakBinderPeer::new(binder_proc, binder_thread),
                data: transaction,
            }
        };

        // Acquire an exclusive lock to prevent a thread from being scheduled twice.
        let target_thread_pool = target_proc.thread_pool.write();

        // Find a thread to handle the transaction, or use the process' command queue.
        if let Some(target_thread) = caller_thread {
            target_thread.write().enqueue_command(command);
        } else if let Some(target_thread) = target_thread_pool.find_available_thread() {
            target_thread.write().enqueue_command(command);
        } else {
            target_proc.enqueue_command(command);
        }
        Ok(())
    }

    /// A binder thread is sending a reply to a transaction.
    fn handle_reply(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        data: binder_transaction_data_sg,
    ) -> Result<(), TransactionError> {
        // Find the process and thread that initiated the transaction. This reply is for them.
        let (target_proc, target_thread) = binder_thread.read().transaction_caller()?;

        let target_task = current_task
            .kernel()
            .pids
            .read()
            .get_task(target_proc.pid)
            .ok_or_else(|| errno!(EINVAL))?;

        // Copy the transaction data to the target process.
        let (data_buffer, offsets_buffer, transaction_state) = self.copy_transaction_buffers(
            current_task,
            binder_proc,
            binder_thread,
            &target_task,
            &target_proc,
            &data,
        )?;

        // Register the transaction buffer.
        target_proc.active_transactions.lock().insert(
            data_buffer.address,
            ActiveTransaction {
                request_type: RequestType::RequestResponse,
                _state: transaction_state.into(),
            },
        );

        // Schedule the transaction on the target process' command queue.
        target_thread.write().enqueue_command(Command::Reply(TransactionData {
            peer_pid: binder_proc.pid,
            peer_tid: binder_thread.tid,
            peer_euid: current_task.creds().euid,

            object: FlatBinderObject::Remote { handle: Handle::SpecialServiceManager },
            code: data.transaction_data.code,
            flags: data.transaction_data.flags,

            data_buffer,
            offsets_buffer,
        }));

        // Schedule the transaction complete command on the caller's command queue.
        binder_thread.write().enqueue_command(Command::TransactionComplete);

        Ok(())
    }

    /// Dequeues a command from the thread's command queue, or blocks until commands are available.
    fn handle_thread_read(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        read_buffer: &UserBuffer,
    ) -> Result<usize, Errno> {
        loop {
            // THREADING: Always acquire the [`BinderProcess::command_queue`] lock before the
            // [`BinderThread::state`] lock or else it may lead to deadlock.
            let mut proc_command_queue = binder_proc.command_queue.lock();
            let mut thread_state = binder_thread.write();

            // Select which command queue to read from, preferring the thread-local one.
            // If a transaction is pending, deadlocks can happen if reading from the process queue.
            let command_queue = if !thread_state.command_queue.is_empty()
                || !thread_state.transactions.is_empty()
            {
                &mut thread_state.command_queue
            } else {
                &mut *proc_command_queue
            };

            if let Some(command) = command_queue.pop_front() {
                // Attempt to write the command to the thread's buffer.
                let bytes_written = command.write_to_memory(&current_task.mm, read_buffer)?;

                match command {
                    Command::Transaction { sender, .. } => {
                        // The transaction is synchronous and we're expected to give a reply, so
                        // push the transaction onto the transaction stack.
                        let tx = TransactionRole::Receiver(sender);
                        thread_state.transactions.push(tx);
                    }
                    Command::Reply(..) | Command::TransactionComplete => {
                        // A transaction is complete, pop it from the transaction stack.
                        thread_state.transactions.pop().expect("transaction stack underflow!");
                    }
                    Command::OnewayTransaction(..)
                    | Command::OnewayTransactionComplete
                    | Command::AcquireRef(..)
                    | Command::ReleaseRef(..)
                    | Command::Error(..)
                    | Command::FailedReply
                    | Command::DeadReply
                    | Command::DeadBinder(..) => {}
                }

                return Ok(bytes_written);
            }

            // No commands readily available to read. Wait for work.
            let waiter = Waiter::new();
            thread_state.waiter = waiter.weak();
            drop(thread_state);
            drop(proc_command_queue);

            // Put this thread to sleep.
            scopeguard::defer! {
                binder_thread.write().waiter = WaiterRef::empty();
            }
            waiter.wait(current_task)?;
        }
    }

    /// Copies transaction buffers from the source process' address space to a new buffer in the
    /// target process' shared binder VMO.
    /// Returns a pair of addresses, the first the address to the transaction data, the second the
    /// address to the offset buffer.
    fn copy_transaction_buffers<'a>(
        &self,
        source_task: &Task,
        source_proc: &Arc<BinderProcess>,
        source_thread: &Arc<BinderThread>,
        target_task: &'a Task,
        target_proc: &Arc<BinderProcess>,
        data: &binder_transaction_data_sg,
    ) -> Result<(UserBuffer, UserBuffer, TransientTransactionState<'a>), TransactionError> {
        // Get the shared memory of the target process.
        let mut shared_memory_lock = target_proc.shared_memory.lock();
        let shared_memory = shared_memory_lock.as_mut().ok_or_else(|| errno!(ENOMEM))?;

        // Allocate a buffer from the target process' shared memory.
        let (mut data_buffer, mut offsets_buffer, mut sg_buffer) = shared_memory.allocate_buffers(
            data.transaction_data.data_size as usize,
            data.transaction_data.offsets_size as usize,
            data.buffers_size as usize,
        )?;

        // SAFETY: `binder_transaction_data` was read from a userspace VMO, which means that all
        // bytes are defined, making union access safe (even if the value is garbage).
        let userspace_addrs = unsafe { data.transaction_data.data.ptr };

        // Copy the data straight into the target's buffer.
        source_task
            .mm
            .read_memory(UserAddress::from(userspace_addrs.buffer), data_buffer.as_mut_bytes())?;
        source_task.mm.read_objects(
            UserRef::new(UserAddress::from(userspace_addrs.offsets)),
            offsets_buffer.as_mut_bytes(),
        )?;

        // Translate any handles/fds from the source process' handle table to the target process'
        // handle table.
        let transient_transaction_state = self.translate_handles(
            source_task,
            source_proc,
            source_thread,
            target_task,
            target_proc,
            offsets_buffer.as_bytes(),
            data_buffer.as_mut_bytes(),
            &mut sg_buffer,
        )?;

        Ok((data_buffer.user_buffer(), offsets_buffer.user_buffer(), transient_transaction_state))
    }

    /// Translates binder object handles/FDs from the sending process to the receiver process,
    /// patching the transaction data as needed.
    ///
    /// When a binder object is sent from one process to another, it must be added to the receiving
    /// process' handle table. Conversely, a handle being sent to the process that owns the
    /// underlying binder object should receive the actual pointers to the object.
    ///
    /// Returns [`TransientTransactionState`], which contains the handles in the target process'
    /// handle table for which temporary strong references were acquired, along with duped FDs. This
    /// object takes care of releasing these resources when dropped, due to an error or a
    /// `BC_FREE_BUFFER` command.
    fn translate_handles<'a>(
        &self,
        source_task: &Task,
        source_proc: &Arc<BinderProcess>,
        source_thread: &Arc<BinderThread>,
        target_task: &'a Task,
        target_proc: &Arc<BinderProcess>,
        offsets: &[binder_uintptr_t],
        transaction_data: &mut [u8],
        sg_buffer: &mut SharedBuffer<'_, u8>,
    ) -> Result<TransientTransactionState<'a>, TransactionError> {
        let mut transaction_state = TransientTransactionState::new(target_task, target_proc);

        let mut sg_remaining_buffer = sg_buffer.user_buffer();
        let mut sg_buffer_offset = 0;
        for (offset_idx, object_offset) in offsets.iter().map(|o| *o as usize).enumerate() {
            // Bounds-check the offset.
            if object_offset >= transaction_data.len() {
                return error!(EINVAL)?;
            }
            let serialized_object =
                SerializedBinderObject::from_bytes(&transaction_data[object_offset..])?;
            let translated_object = match serialized_object {
                SerializedBinderObject::Handle { handle, flags, cookie } => {
                    match handle {
                        Handle::SpecialServiceManager => {
                            // The special handle 0 does not need to be translated. It is universal.
                            serialized_object
                        }
                        Handle::Object { index } => {
                            let proxy = source_proc
                                .handles
                                .lock()
                                .get(index)
                                .ok_or(TransactionError::Failure)?;
                            if std::ptr::eq(Arc::as_ptr(target_proc), proxy.owner.as_ptr()) {
                                // The binder object belongs to the receiving process, so convert it
                                // from a handle to a local object.
                                SerializedBinderObject::Object { local: proxy.local, flags }
                            } else {
                                // The binder object does not belong to the receiving process, so
                                // dup the handle in the receiving process' handle table.
                                let new_handle =
                                    target_proc.handles.lock().insert_for_transaction(proxy);

                                // Tie this handle's strong reference to be held as long as this
                                // buffer.
                                transaction_state.push_handle(new_handle);

                                SerializedBinderObject::Handle { handle: new_handle, flags, cookie }
                            }
                        }
                    }
                }
                SerializedBinderObject::Object { local, flags } => {
                    // We are passing a binder object across process boundaries. We need
                    // to translate this address to some handle.

                    // Register this binder object if it hasn't already been registered.
                    let object = source_proc.find_or_register_object(local);

                    // Tell the owning process that a remote process now has a strong reference to
                    // to this object.
                    source_thread.state.write().enqueue_command(Command::AcquireRef(local));

                    // Create a handle in the receiving process that references the binder object
                    // in the sender's process.
                    let handle = target_proc.handles.lock().insert_for_transaction(object);

                    // Tie this handle's strong reference to be held as long as this buffer.
                    transaction_state.push_handle(handle);

                    // Translate the serialized object into a handle.
                    SerializedBinderObject::Handle { handle, flags, cookie: 0 }
                }
                SerializedBinderObject::File { fd, flags, cookie } => {
                    let (file, fd_flags) = source_task.files.get_with_flags(fd)?;
                    let new_fd = target_task.files.add_with_flags(file, fd_flags)?;

                    // Close this FD if the transaction fails.
                    transaction_state.push_fd(new_fd);

                    SerializedBinderObject::File { fd: new_fd, flags, cookie }
                }
                SerializedBinderObject::Buffer { buffer, length, flags, parent, parent_offset } => {
                    // Copy the memory pointed to by this buffer object into the receiver.
                    if length > sg_remaining_buffer.length {
                        return error!(EINVAL)?;
                    }
                    source_task.mm.read_memory(
                        buffer,
                        &mut sg_buffer.as_mut_bytes()[sg_buffer_offset..sg_buffer_offset + length],
                    )?;

                    let translated_buffer_address = sg_remaining_buffer.address;

                    // If the buffer has a parent, it means that the parent buffer has a pointer to
                    // this buffer. This pointer will need to be translated to the receiver's
                    // address space.
                    if flags & BINDER_BUFFER_FLAG_HAS_PARENT != 0 {
                        // The parent buffer must come earlier in the object list and already be
                        // copied into the receiver's address space. Otherwise we would be fixing
                        // up memory in the sender's address space, which is marked const in the
                        // userspace runtime.
                        if parent >= offset_idx {
                            return error!(EINVAL)?;
                        }

                        // Find the parent buffer payload. There is a pointer in the buffer
                        // that points to this object.
                        let parent_buffer_payload = find_parent_buffer(
                            transaction_data,
                            sg_buffer,
                            offsets[parent] as usize,
                        )?;

                        // Bounds-check that the offset is within the buffer.
                        if parent_offset >= parent_buffer_payload.len() {
                            return error!(EINVAL)?;
                        }

                        // Patch the pointer with the translated address.
                        translated_buffer_address
                            .write_to_prefix(&mut parent_buffer_payload[parent_offset..])
                            .ok_or_else(|| errno!(EINVAL))?;
                    }

                    // Update the scatter-gather buffer to account for the buffer we just wrote.
                    // We pad the length of this buffer so that the next buffer starts at an aligned
                    // offset.
                    let padded_length =
                        round_up_to_increment(length, std::mem::size_of::<binder_uintptr_t>())?;
                    sg_remaining_buffer = UserBuffer {
                        address: sg_remaining_buffer.address + padded_length,
                        length: sg_remaining_buffer.length - padded_length,
                    };
                    sg_buffer_offset += padded_length;

                    // Patch this buffer with the translated address.
                    SerializedBinderObject::Buffer {
                        buffer: translated_buffer_address,
                        length,
                        flags,
                        parent,
                        parent_offset,
                    }
                }
                SerializedBinderObject::FileArray { num_fds, parent, parent_offset } => {
                    // The parent buffer must come earlier in the object list and already be
                    // copied into the receiver's address space. Otherwise we would be fixing
                    // up memory in the sender's address space, which is marked const in the
                    // userspace runtime.
                    if parent >= offset_idx {
                        return error!(EINVAL)?;
                    }

                    // Find the parent buffer payload. The file descriptor array is in here.
                    let parent_buffer_payload =
                        find_parent_buffer(transaction_data, sg_buffer, offsets[parent] as usize)?;

                    // Bounds-check that the offset is within the buffer.
                    if parent_offset >= parent_buffer_payload.len() {
                        return error!(EINVAL)?;
                    }

                    // Verify alignment and size before reading the data as a [u32].
                    let (layout, _) =
                        zerocopy::LayoutVerified::<&mut [u8], [u32]>::new_slice_from_prefix(
                            &mut parent_buffer_payload[parent_offset..],
                            num_fds,
                        )
                        .ok_or_else(|| errno!(EINVAL))?;
                    let fd_array = layout.into_mut_slice();

                    // Dup each file descriptor and re-write the value of the new FD.
                    for fd in fd_array {
                        let (file, flags) =
                            source_task.files.get_with_flags(FdNumber::from_raw(*fd as i32))?;
                        let new_fd = target_task.files.add_with_flags(file, flags)?;

                        // Close this FD if the transaction fails.
                        transaction_state.push_fd(new_fd);

                        *fd = new_fd.raw() as u32;
                    }

                    SerializedBinderObject::FileArray { num_fds, parent, parent_offset }
                }
            };

            translated_object.write_to(&mut transaction_data[object_offset..])?;
        }

        Ok(transaction_state)
    }

    fn mmap(
        &self,
        current_task: &CurrentTask,
        binder_proc: &Arc<BinderProcess>,
        addr: DesiredAddress,
        length: usize,
        flags: zx::VmarFlags,
        mapping_options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        // Do not support mapping shared memory more than once.
        let mut shared_memory = binder_proc.shared_memory.lock();
        if shared_memory.is_some() {
            return error!(EINVAL);
        }

        // Create a VMO that will be shared between the driver and the client process.
        let vmo = Arc::new(zx::Vmo::create(length as u64).map_err(|_| errno!(ENOMEM))?);

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
        match SharedMemory::map(&vmo, user_address, length) {
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
        binder_proc: &Arc<BinderProcess>,
        binder_thread: &Arc<BinderThread>,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        _options: WaitAsyncOptions,
    ) -> WaitKey {
        // THREADING: Always acquire the [`BinderProcess::command_queue`] lock before the
        // [`BinderThread::state`] lock or else it may lead to deadlock.
        let proc_command_queue = binder_proc.command_queue.lock();
        let thread_state = binder_thread.write();

        if proc_command_queue.is_empty() && thread_state.command_queue.is_empty() {
            binder_proc.waiters.lock().wait_async_events(waiter, events, handler)
        } else {
            waiter.wake_immediately(FdEvents::POLLIN.mask(), handler)
        }
    }

    fn cancel_wait(&self, binder_proc: &Arc<BinderProcess>, key: WaitKey) {
        binder_proc.waiters.lock().cancel_wait(key);
    }
}

/// Finds a buffer object's payload in the transaction. The buffer object describing the payload is
/// deserialized from `transaction_data` at `buffer_object_offset`. The actual payload is located in
/// `sg_buffer`. The buffer object must have already been validated and its payload copied to
/// `sg_buffer`. This is true for parent objects, as they are required to be processed before being
/// referenced by child objects.
fn find_parent_buffer<'a>(
    transaction_data: &[u8],
    sg_buffer: &mut SharedBuffer<'a, u8>,
    buffer_object_offset: usize,
) -> Result<&'a mut [u8], Errno> {
    // The buffer object has already been validated, since the requirement is that parent objects
    // are processed before their children. In addition, the payload has been written by us, so it
    // should be guaranteed to be valid. Still, it is possible for userspace to mutate this memory
    // while we are processing it, so we still perform checked arithmetic to avoid panics in
    // starnix.

    // Verify that the offset is within the transaction data.
    if buffer_object_offset >= transaction_data.len() {
        return error!(EINVAL);
    }

    // Deserialize the parent object buffer and extract the relevant data.
    let (buffer_payload_addr, buffer_payload_length) =
        match SerializedBinderObject::from_bytes(&transaction_data[buffer_object_offset..])? {
            SerializedBinderObject::Buffer { buffer, length, .. } => (buffer, length),
            _ => return error!(EINVAL)?,
        };

    // Calculate the start and end of the buffer payload in the scatter gather buffer.
    // The buffer payload will have been copied to the scatter gather buffer, so recover the
    // offset from its userspace address.
    if buffer_payload_addr < sg_buffer.user_buffer().address {
        // This should never happen unless userspace is messing with us, since we wrote this address
        // during translation.
        return error!(EINVAL);
    }
    let buffer_payload_start = buffer_payload_addr - sg_buffer.user_buffer().address;
    let buffer_payload_end =
        buffer_payload_start.checked_add(buffer_payload_length).ok_or_else(|| errno!(EINVAL))?;

    // Return a slice that represents the parent buffer.
    Ok(&mut sg_buffer.as_mut_bytes()[buffer_payload_start..buffer_payload_end])
}

/// Represents a serialized binder object embedded in transaction data.
#[derive(Debug, PartialEq, Eq)]
enum SerializedBinderObject {
    /// A `BINDER_TYPE_HANDLE` object. A handle to a remote binder object.
    Handle { handle: Handle, flags: u32, cookie: binder_uintptr_t },
    /// A `BINDER_TYPE_BINDER` object. The in-process representation of a binder object.
    Object { local: LocalBinderObject, flags: u32 },
    /// A `BINDER_TYPE_FD` object. A file descriptor.
    File { fd: FdNumber, flags: u32, cookie: binder_uintptr_t },
    /// A `BINDER_TYPE_PTR` object. Identifies a pointer in the transaction data that needs to be
    /// fixed up when the payload is copied into the destination process. Part of the scatter-gather
    /// implementation.
    Buffer { buffer: UserAddress, length: usize, parent: usize, parent_offset: usize, flags: u32 },
    /// A `BINDER_TYPE_FDA` object. Identifies an array of file descriptors in a parent buffer that
    /// must be duped into the receiver's file descriptor table.
    FileArray { num_fds: usize, parent: usize, parent_offset: usize },
}

impl SerializedBinderObject {
    /// Deserialize a binder object from `data`. `data` must be large enough to fit the size of the
    /// serialized object, or else this method fails.
    fn from_bytes(data: &[u8]) -> Result<Self, Errno> {
        let object_header =
            binder_object_header::read_from_prefix(data).ok_or_else(|| errno!(EINVAL))?;
        match object_header.type_ {
            BINDER_TYPE_BINDER => {
                let object =
                    flat_binder_object::read_from_prefix(data).ok_or_else(|| errno!(EINVAL))?;
                Ok(Self::Object {
                    local: LocalBinderObject {
                        // SAFETY: Union read.
                        weak_ref_addr: UserAddress::from(unsafe { object.__bindgen_anon_1.binder }),
                        strong_ref_addr: UserAddress::from(object.cookie),
                    },
                    flags: object.flags,
                })
            }
            BINDER_TYPE_HANDLE => {
                let object =
                    flat_binder_object::read_from_prefix(data).ok_or_else(|| errno!(EINVAL))?;
                Ok(Self::Handle {
                    // SAFETY: Union read.
                    handle: unsafe { object.__bindgen_anon_1.handle }.into(),
                    flags: object.flags,
                    cookie: object.cookie,
                })
            }
            BINDER_TYPE_FD => {
                let object =
                    flat_binder_object::read_from_prefix(data).ok_or_else(|| errno!(EINVAL))?;
                Ok(Self::File {
                    // SAFETY: Union read.
                    fd: FdNumber::from_raw(unsafe { object.__bindgen_anon_1.handle } as i32),
                    flags: object.flags,
                    cookie: object.cookie,
                })
            }
            BINDER_TYPE_PTR => {
                let object =
                    binder_buffer_object::read_from_prefix(data).ok_or_else(|| errno!(EINVAL))?;
                Ok(Self::Buffer {
                    buffer: UserAddress::from(object.buffer),
                    length: object.length as usize,
                    parent: object.parent as usize,
                    parent_offset: object.parent_offset as usize,
                    flags: object.flags,
                })
            }
            BINDER_TYPE_FDA => {
                let object =
                    binder_fd_array_object::read_from_prefix(data).ok_or_else(|| errno!(EINVAL))?;
                Ok(Self::FileArray {
                    num_fds: object.num_fds as usize,
                    parent: object.parent as usize,
                    parent_offset: object.parent_offset as usize,
                })
            }
            object_type => {
                tracing::error!("unknown object type 0x{:08x}", object_type);
                error!(EINVAL)
            }
        }
    }

    /// Writes the serialized object back to `data`. `data` must be large enough to fit the
    /// serialized object, or else this method fails.
    fn write_to(self, data: &mut [u8]) -> Result<(), Errno> {
        match self {
            SerializedBinderObject::Handle { handle, flags, cookie } => {
                struct_with_union_into_bytes!(flat_binder_object {
                    hdr.type_: BINDER_TYPE_HANDLE,
                    __bindgen_anon_1.handle: handle.into(),
                    flags: flags,
                    cookie: cookie,
                })
                .write_to_prefix(data)
            }
            SerializedBinderObject::Object { local, flags } => {
                struct_with_union_into_bytes!(flat_binder_object {
                    hdr.type_: BINDER_TYPE_BINDER,
                    __bindgen_anon_1.binder: local.weak_ref_addr.ptr() as u64,
                    flags: flags,
                    cookie: local.strong_ref_addr.ptr() as u64,
                })
                .write_to_prefix(data)
            }
            SerializedBinderObject::File { fd, flags, cookie } => {
                struct_with_union_into_bytes!(flat_binder_object {
                    hdr.type_: BINDER_TYPE_FD,
                    __bindgen_anon_1.handle: fd.raw() as u32,
                    flags: flags,
                    cookie: cookie,
                })
                .write_to_prefix(data)
            }
            SerializedBinderObject::Buffer { buffer, length, parent, parent_offset, flags } => {
                binder_buffer_object {
                    hdr: binder_object_header { type_: BINDER_TYPE_PTR },
                    buffer: buffer.ptr() as u64,
                    length: length as u64,
                    parent: parent as u64,
                    parent_offset: parent_offset as u64,
                    flags,
                }
                .write_to_prefix(data)
            }
            SerializedBinderObject::FileArray { num_fds, parent, parent_offset } => {
                binder_fd_array_object {
                    hdr: binder_object_header { type_: BINDER_TYPE_FDA },
                    pad: 0,
                    num_fds: num_fds as u64,
                    parent: parent as u64,
                    parent_offset: parent_offset as u64,
                }
                .write_to_prefix(data)
            }
        }
        .ok_or_else(|| errno!(EINVAL))
    }
}

/// An error processing a binder transaction/reply.
///
/// Some errors, like a malformed transaction request, should be propagated as the return value of
/// an ioctl. Other errors, like a dead recipient or invalid binder handle, should be propagated
/// through a command read by the binder thread.
///
/// This type differentiates between these strategies.
#[derive(Debug, Eq, PartialEq)]
enum TransactionError {
    /// The transaction payload was malformed. Send a [`Command::Error`] command to the issuing
    /// thread.
    Malformed(Errno),
    /// The transaction payload was correctly formed, but either the recipient, or a handle embedded
    /// in the transaction, is invalid. Send a [`Command::FailedReply`] command to the issuing
    /// thread.
    Failure,
    /// The transaction payload was correctly formed, but either the recipient, or a handle embedded
    /// in the transaction, is dead. Send a [`Command::DeadReply`] command to the issuing thread.
    Dead,
}

impl TransactionError {
    /// Dispatches the error, by potentially queueing a command to `binder_thread` and/or returning
    /// an error.
    fn dispatch(self, binder_thread: &Arc<BinderThread>) -> Result<(), Errno> {
        binder_thread.write().enqueue_command(match self {
            TransactionError::Malformed(err) => {
                tracing::warn!(
                    "binder thread {} sent a malformed transaction: {:?}",
                    binder_thread.tid,
                    &err
                );
                // Negate the value, as the binder runtime assumes error values are already
                // negative.
                Command::Error(err.return_value() as i32)
            }
            TransactionError::Failure => Command::FailedReply,
            TransactionError::Dead => Command::DeadReply,
        });
        Ok(())
    }
}

impl From<Errno> for TransactionError {
    fn from(errno: Errno) -> TransactionError {
        TransactionError::Malformed(errno)
    }
}

pub struct BinderFs;
impl FileSystemOps for BinderFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs::default(BINDERFS_SUPER_MAGIC))
    }
}

struct BinderFsDir;
impl FsNodeOps for BinderFsDir {
    fs_node_impl_dir_readonly!();

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(MemoryDirectoryFile::new()))
    }
}

const BINDERS: &[&FsStr] = &[b"binder", b"hwbinder", b"vndbinder"];

fn make_binder_nodes(kernel: &Kernel, dir: &DirEntryHandle) -> Result<(), Errno> {
    for name in BINDERS {
        let dev = kernel.device_registry.write().register_dyn_chrdev(BinderDriver::new())?;
        dir.add_node_ops_dev(name, mode!(IFCHR, 0o600), dev, SpecialNode)?;
    }
    Ok(())
}

impl BinderFs {
    pub fn new_fs(kernel: &Kernel) -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(kernel, BinderFs);
        fs.set_root(BinderFsDir);
        make_binder_nodes(kernel, fs.root())?;
        Ok(fs)
    }
}

pub fn create_binders(current_task: &CurrentTask) -> Result<(), Errno> {
    let fs = dev_tmp_fs(current_task);
    make_binder_nodes(current_task.kernel(), fs.root())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::{DirEntry, FdFlags};
    use crate::mm::PAGE_SIZE;
    use crate::testing::*;
    use assert_matches::assert_matches;
    use memoffset::offset_of;

    const BASE_ADDR: UserAddress = UserAddress::from(0x0000000000000100);
    const VMO_LENGTH: usize = 4096;

    #[fuchsia::test]
    fn handle_tests() {
        assert_matches!(Handle::from(0), Handle::SpecialServiceManager);
        assert_matches!(Handle::from(1), Handle::Object { index: 0 });
        assert_matches!(Handle::from(2), Handle::Object { index: 1 });
        assert_matches!(Handle::from(99), Handle::Object { index: 98 });
    }

    #[fuchsia::test]
    fn handle_0_fails_when_context_manager_is_not_set() {
        let driver = BinderDriver::new();
        assert_eq!(
            driver.get_context_manager().expect_err("unexpectedly succeeded"),
            errno!(ENOENT),
        );
    }

    #[fuchsia::test]
    fn handle_0_succeeds_when_context_manager_is_set() {
        let driver = BinderDriver::new();
        let context_manager_proc = driver.create_process(1);
        let context_manager = Arc::new(BinderObject::new(
            &context_manager_proc,
            LocalBinderObject {
                weak_ref_addr: UserAddress::from(0xDEADBEEF),
                strong_ref_addr: UserAddress::from(0xDEADDEAD),
            },
        ));
        *driver.context_manager.write() = Some(context_manager);
        let (object, owner) = driver.get_context_manager().expect("failed to find handle 0");
        assert!(Arc::ptr_eq(&context_manager_proc, &owner));
        assert_eq!(object.local.weak_ref_addr, UserAddress::from(0xDEADBEEF));
        assert_eq!(object.local.strong_ref_addr, UserAddress::from(0xDEADDEAD));
    }

    #[fuchsia::test]
    fn fail_to_retrieve_non_existing_handle() {
        let driver = BinderDriver::new();
        let binder_proc = driver.create_process(1);
        assert!(binder_proc.handles.lock().get(3).is_none());
    }

    #[fuchsia::test]
    fn handle_is_not_dropped_after_transaction_finishes_if_it_already_existed() {
        let driver = BinderDriver::new();
        let proc_1 = driver.create_process(1);
        let proc_2 = driver.create_process(2);

        let transaction_ref = Arc::new(BinderObject::new(
            &proc_1,
            LocalBinderObject {
                weak_ref_addr: UserAddress::from(0xffffffffffffffff),
                strong_ref_addr: UserAddress::from(0x1111111111111111),
            },
        ));

        // Insert the transaction once.
        let _ = proc_2.handles.lock().insert_for_transaction(transaction_ref.clone());

        // Insert the same object.
        let handle = proc_2.handles.lock().insert_for_transaction(transaction_ref.clone());

        // The object should be present in the handle table until a strong decrement.
        assert!(Arc::ptr_eq(
            &proc_2.handles.lock().get(handle.object_index()).expect("valid object"),
            &transaction_ref
        ));

        // Drop the transaction reference.
        proc_2.handles.lock().dec_strong(handle.object_index()).expect("dec_strong");

        // The handle should not have been dropped, as it was already in the table beforehand.
        assert!(Arc::ptr_eq(
            &proc_2.handles.lock().get(handle.object_index()).expect("valid object"),
            &transaction_ref
        ));
    }

    #[fuchsia::test]
    fn handle_is_dropped_after_transaction_finishes() {
        let driver = BinderDriver::new();
        let proc_1 = driver.create_process(1);
        let proc_2 = driver.create_process(2);

        let transaction_ref = Arc::new(BinderObject::new(
            &proc_1,
            LocalBinderObject {
                weak_ref_addr: UserAddress::from(0xffffffffffffffff),
                strong_ref_addr: UserAddress::from(0x1111111111111111),
            },
        ));

        // Transactions always take a strong reference to binder objects.
        let handle = proc_2.handles.lock().insert_for_transaction(transaction_ref.clone());

        // The object should be present in the handle table until a strong decrement.
        assert!(Arc::ptr_eq(
            &proc_2.handles.lock().get(handle.object_index()).expect("valid object"),
            &transaction_ref
        ));

        // Drop the transaction reference.
        proc_2.handles.lock().dec_strong(handle.object_index()).expect("dec_strong");

        // The handle should now have been dropped.
        assert!(proc_2.handles.lock().get(handle.object_index()).is_none());
    }

    #[fuchsia::test]
    fn handle_is_dropped_after_last_weak_ref_released() {
        let driver = BinderDriver::new();
        let proc_1 = driver.create_process(1);
        let proc_2 = driver.create_process(2);

        let transaction_ref = Arc::new(BinderObject::new(
            &proc_1,
            LocalBinderObject {
                weak_ref_addr: UserAddress::from(0xffffffffffffffff),
                strong_ref_addr: UserAddress::from(0x1111111111111111),
            },
        ));

        // The handle starts with a strong ref.
        let handle = proc_2.handles.lock().insert_for_transaction(transaction_ref.clone());

        // Acquire a weak reference.
        proc_2.handles.lock().inc_weak(handle.object_index()).expect("inc_weak");

        // The object should be present in the handle table.
        assert!(Arc::ptr_eq(
            &proc_2.handles.lock().get(handle.object_index()).expect("valid object"),
            &transaction_ref
        ));

        // Drop the strong reference. The handle should still be present as there is an outstanding
        // weak reference.
        proc_2.handles.lock().dec_strong(handle.object_index()).expect("dec_strong");
        assert!(Arc::ptr_eq(
            &proc_2.handles.lock().get(handle.object_index()).expect("valid object"),
            &transaction_ref
        ));

        // Drop the weak reference. The handle should now be gone, even though the underlying object
        // is still alive (another process could have references to it).
        proc_2.handles.lock().dec_weak(handle.object_index()).expect("dec_weak");
        assert!(
            proc_2.handles.lock().get(handle.object_index()).is_none(),
            "handle should be dropped"
        );
    }

    #[fuchsia::test]
    fn shared_memory_allocation_fails_with_invalid_offsets_length() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");
        shared_memory
            .allocate_buffers(3, 1, 0)
            .expect_err("offsets_length should be multiple of 8");
        shared_memory
            .allocate_buffers(3, 8, 1)
            .expect_err("buffers_length should be multiple of 8");
    }

    #[fuchsia::test]
    fn shared_memory_allocation_aligns_offsets_buffer() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        const DATA_LEN: usize = 3;
        const OFFSETS_COUNT: usize = 1;
        const OFFSETS_LEN: usize = std::mem::size_of::<binder_uintptr_t>() * OFFSETS_COUNT;
        const BUFFERS_LEN: usize = 8;
        let (data_buf, offsets_buf, buffers_buf) = shared_memory
            .allocate_buffers(DATA_LEN, OFFSETS_LEN, BUFFERS_LEN)
            .expect("allocate buffer");
        assert_eq!(data_buf.user_buffer(), UserBuffer { address: BASE_ADDR, length: DATA_LEN });
        assert_eq!(
            offsets_buf.user_buffer(),
            UserBuffer { address: BASE_ADDR + 8usize, length: OFFSETS_LEN }
        );
        assert_eq!(
            buffers_buf.user_buffer(),
            UserBuffer { address: BASE_ADDR + 8usize + OFFSETS_LEN, length: BUFFERS_LEN }
        );
        assert_eq!(data_buf.as_bytes().len(), DATA_LEN);
        assert_eq!(offsets_buf.as_bytes().len(), OFFSETS_COUNT);
        assert_eq!(buffers_buf.as_bytes().len(), BUFFERS_LEN);
    }

    #[fuchsia::test]
    fn shared_memory_allocation_buffers_correctly_write_through() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        const DATA_LEN: usize = 256;
        const OFFSETS_COUNT: usize = 4;
        const OFFSETS_LEN: usize = std::mem::size_of::<binder_uintptr_t>() * OFFSETS_COUNT;
        let (mut data_buf, mut offsets_buf, _) =
            shared_memory.allocate_buffers(DATA_LEN, OFFSETS_LEN, 0).expect("allocate buffer");

        // Write data to the allocated buffers.
        const DATA_FILL: u8 = 0xff;
        data_buf.as_mut_bytes().fill(0xff);

        const OFFSETS_FILL: binder_uintptr_t = 0xDEADBEEFDEADBEEF;
        offsets_buf.as_mut_bytes().fill(OFFSETS_FILL);

        // Check that the correct bit patterns were written through to the underlying VMO.
        let mut data = [0u8; DATA_LEN];
        vmo.read(&mut data, 0).expect("read VMO failed");
        assert!(data.iter().all(|b| *b == DATA_FILL));

        let mut data = [0u64; OFFSETS_COUNT];
        vmo.read(data.as_bytes_mut(), DATA_LEN as u64).expect("read VMO failed");
        assert!(data.iter().all(|b| *b == OFFSETS_FILL));
    }

    #[fuchsia::test]
    fn shared_memory_allocates_multiple_buffers() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        // Check that two buffers allocated from the same shared memory region don't overlap.
        const BUF1_DATA_LEN: usize = 64;
        const BUF1_OFFSETS_LEN: usize = 8;
        const BUF1_BUFFERS_LEN: usize = 8;
        let (data_buf, offsets_buf, buffers_buf) = shared_memory
            .allocate_buffers(BUF1_DATA_LEN, BUF1_OFFSETS_LEN, BUF1_BUFFERS_LEN)
            .expect("allocate buffer 1");
        assert_eq!(
            data_buf.user_buffer(),
            UserBuffer { address: BASE_ADDR, length: BUF1_DATA_LEN }
        );
        assert_eq!(
            offsets_buf.user_buffer(),
            UserBuffer { address: BASE_ADDR + BUF1_DATA_LEN, length: BUF1_OFFSETS_LEN }
        );
        assert_eq!(
            buffers_buf.user_buffer(),
            UserBuffer {
                address: BASE_ADDR + BUF1_DATA_LEN + BUF1_OFFSETS_LEN,
                length: BUF1_BUFFERS_LEN
            }
        );

        const BUF2_DATA_LEN: usize = 32;
        const BUF2_OFFSETS_LEN: usize = 0;
        const BUF2_BUFFERS_LEN: usize = 0;
        let (data_buf, offsets_buf, _) = shared_memory
            .allocate_buffers(BUF2_DATA_LEN, BUF2_OFFSETS_LEN, BUF2_BUFFERS_LEN)
            .expect("allocate buffer 2");
        assert_eq!(
            data_buf.user_buffer(),
            UserBuffer {
                address: BASE_ADDR + BUF1_DATA_LEN + BUF1_OFFSETS_LEN + BUF1_BUFFERS_LEN,
                length: BUF2_DATA_LEN
            }
        );
        assert_eq!(
            offsets_buf.user_buffer(),
            UserBuffer {
                address: BASE_ADDR
                    + BUF1_DATA_LEN
                    + BUF1_OFFSETS_LEN
                    + BUF1_BUFFERS_LEN
                    + BUF2_DATA_LEN,
                length: BUF2_OFFSETS_LEN
            }
        );
    }

    #[fuchsia::test]
    fn shared_memory_too_large_allocation_fails() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        shared_memory.allocate_buffers(VMO_LENGTH + 1, 0, 0).expect_err("out-of-bounds allocation");
        shared_memory.allocate_buffers(VMO_LENGTH, 8, 0).expect_err("out-of-bounds allocation");
        shared_memory.allocate_buffers(VMO_LENGTH - 8, 8, 8).expect_err("out-of-bounds allocation");

        shared_memory.allocate_buffers(VMO_LENGTH, 0, 0).expect("allocate buffer");

        // Now that the previous buffer allocation succeeded, there should be no more room.
        shared_memory.allocate_buffers(1, 0, 0).expect_err("out-of-bounds allocation");
    }

    /// Fills the provided shared memory with n buffers, each spanning 1/n-th of the vmo.
    fn fill_with_buffers(shared_memory: &mut SharedMemory, n: usize) -> Vec<UserAddress> {
        let mut addresses = vec![];
        for _ in 0..n {
            let address = {
                let (buffer, _, _) = shared_memory
                    .allocate_buffers(VMO_LENGTH / n, 0, 0)
                    .unwrap_or_else(|_| panic!("allocate {:?}-th buffer", n));
                buffer.memory.user_address + buffer.offset
            };
            addresses.push(address);
        }
        tracing::warn!("{:?}", addresses);
        addresses
    }

    #[fuchsia::test]
    fn shared_memory_allocation_wraps_in_order() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");
        let n = 4;

        for buffer in fill_with_buffers(&mut shared_memory, n) {
            shared_memory
                .allocate_buffers(VMO_LENGTH / n, 0, 0)
                .expect_err(&format!("allocated buffer when shared memory was full {:?}", n));

            shared_memory.free_buffer(buffer).expect("didn't free buffer");

            shared_memory.allocate_buffers(VMO_LENGTH / n, 0, 0).unwrap_or_else(|_| {
                panic!("couldn't allocate new buffer even after {:?}-th was released", n)
            });
        }
    }

    #[fuchsia::test]
    fn shared_memory_allocation_single() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");
        let n = 1;
        let buffers = fill_with_buffers(&mut shared_memory, n);

        shared_memory
            .allocate_buffers(VMO_LENGTH / n, 0, 0)
            .expect_err("could allocate when buffer was full");
        shared_memory.free_buffer(buffers[0]).expect("didn't free buffer");
        shared_memory
            .allocate_buffers(VMO_LENGTH / n, 0, 0)
            .expect("couldn't allocate even after first slot opened up");
    }

    #[fuchsia::test]
    fn shared_memory_allocation_doesnt_allocate_out_of_order() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");
        let n = 4;

        let buffers = fill_with_buffers(&mut shared_memory, n);

        // Free all the buffers in reverse order, and verify that the new buffer isn't allocated
        // until the first buffer is freed.
        shared_memory.allocate_buffers(VMO_LENGTH / n, 0, 0).expect_err("allocated buffer 3");
        shared_memory.free_buffer(buffers[3]).expect("didn't free buffer");
        shared_memory.allocate_buffers(VMO_LENGTH / n, 0, 0).expect_err("allocated buffer 2");
        shared_memory.free_buffer(buffers[2]).expect("didn't free buffer");
        shared_memory.allocate_buffers(VMO_LENGTH / n, 0, 0).expect_err("allocated buffer 1");
        shared_memory.free_buffer(buffers[1]).expect("didn't free buffer");

        shared_memory.free_buffer(buffers[0]).expect("didn't free buffer");
        shared_memory
            .allocate_buffers(VMO_LENGTH / n, 0, 0)
            .expect("couldn't allocate even after first slot opened up");
    }

    #[fuchsia::test]
    fn shared_memory_allocation_doesnt_wrap_when_cant_fit() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");
        let n = 4;

        let buffers = fill_with_buffers(&mut shared_memory, n);

        shared_memory.free_buffer(buffers[0]).expect("didn't free buffer");
        // Allocate slightly more than what can fit at the start (after freeing the first 1/4th).
        shared_memory
            .allocate_buffers((VMO_LENGTH / n) + 1, 0, 0)
            .expect_err("allocated over existing buffer");
        shared_memory.free_buffer(buffers[1]).expect("didn't free buffer");
        shared_memory
            .allocate_buffers((VMO_LENGTH / n) + 1, 0, 0)
            .expect("couldn't allocate when there was enough space");
    }

    #[fuchsia::test]
    fn shared_memory_allocation_doesnt_wrap_when_cant_fit_at_end() {
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        let mut shared_memory =
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory");

        // Test that a buffer can still be allocated even if it can't fit at the end, but can fit
        // at the start by first allocating 3/4 of the vmo.
        let buffer_1 = {
            let (shared_mem, _, _) =
                shared_memory.allocate_buffers(VMO_LENGTH / 4, 0, 0).expect("couldn't allocate");
            shared_mem.memory.user_address + shared_mem.offset
        };
        let buffer_2 = {
            let (shared_mem, _, _) =
                shared_memory.allocate_buffers(VMO_LENGTH / 4, 0, 0).expect("couldn't allocate");
            shared_mem.memory.user_address + shared_mem.offset
        };
        let buffer_3 = {
            let (shared_mem, _, _) =
                shared_memory.allocate_buffers(VMO_LENGTH / 4, 0, 0).expect("couldn't allocate");
            shared_mem.memory.user_address + shared_mem.offset
        };

        // Attempt to allocate a buffer at the end that is larger than 1/4th.
        shared_memory
            .allocate_buffers(VMO_LENGTH / 3, 0, 0)
            .expect_err("allocated over existing buffer");
        // Now free all the buffers at the start.
        shared_memory.free_buffer(buffer_1).expect("didn't free buffer");
        shared_memory.free_buffer(buffer_2).expect("didn't free buffer");
        shared_memory.free_buffer(buffer_3).expect("didn't free buffer");

        // Try the allocation again.
        shared_memory
            .allocate_buffers(VMO_LENGTH / 3, 0, 0)
            .expect("failed even though there was room at start");
    }

    #[fuchsia::test]
    fn binder_object_enqueues_release_command_when_dropped() {
        let driver = BinderDriver::new();
        let proc = driver.create_process(1);

        const LOCAL_BINDER_OBJECT: LocalBinderObject = LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000010),
            strong_ref_addr: UserAddress::from(0x0000000000000100),
        };

        let object = Arc::new(BinderObject::new(&proc, LOCAL_BINDER_OBJECT));

        drop(object);

        assert_matches!(
            proc.command_queue.lock().front(),
            Some(Command::ReleaseRef(LOCAL_BINDER_OBJECT))
        );
    }

    #[fuchsia::test]
    fn handle_table_refs() {
        let driver = BinderDriver::new();
        let proc = driver.create_process(1);

        let object = Arc::new(BinderObject::new(
            &proc,
            LocalBinderObject {
                weak_ref_addr: UserAddress::from(0x0000000000000010),
                strong_ref_addr: UserAddress::from(0x0000000000000100),
            },
        ));

        let mut handle_table = HandleTable::default();

        // Starts with one strong reference.
        let handle = handle_table.insert_for_transaction(object.clone());

        handle_table.inc_strong(handle.object_index()).expect("inc_strong 1");
        handle_table.inc_strong(handle.object_index()).expect("inc_strong 2");
        handle_table.inc_weak(handle.object_index()).expect("inc_weak 0");
        handle_table.dec_strong(handle.object_index()).expect("dec_strong 2");
        handle_table.dec_strong(handle.object_index()).expect("dec_strong 1");

        // Remove the initial strong reference.
        handle_table.dec_strong(handle.object_index()).expect("dec_strong 0");

        // Removing more strong references should fail.
        handle_table.dec_strong(handle.object_index()).expect_err("dec_strong -1");

        // The object should still take up an entry in the handle table, as there is 1 weak
        // reference.
        handle_table.get(handle.object_index()).expect("object still exists");

        drop(object);

        // Our weak reference won't keep the object alive.
        assert!(handle_table.get(handle.object_index()).is_none(), "object should be dead");

        // Remove from our table.
        handle_table.dec_weak(handle.object_index()).expect("dec_weak 0");

        // Another removal attempt will prove the handle has been removed.
        handle_table.dec_weak(handle.object_index()).expect_err("handle should no longer exist");
    }

    #[fuchsia::test]
    fn serialize_binder_handle() {
        let mut output = [0u8; std::mem::size_of::<flat_binder_object>()];

        SerializedBinderObject::Handle { handle: 2.into(), flags: 42, cookie: 99 }
            .write_to(&mut output)
            .expect("write handle");
        assert_eq!(
            struct_with_union_into_bytes!(flat_binder_object {
                hdr.type_: BINDER_TYPE_HANDLE,
                flags: 42,
                cookie: 99,
                __bindgen_anon_1.handle: 2,
            }),
            output
        );
    }

    #[fuchsia::test]
    fn serialize_binder_object() {
        let mut output = [0u8; std::mem::size_of::<flat_binder_object>()];

        SerializedBinderObject::Object {
            local: LocalBinderObject {
                weak_ref_addr: UserAddress::from(0xDEADBEEF),
                strong_ref_addr: UserAddress::from(0xDEADDEAD),
            },
            flags: 42,
        }
        .write_to(&mut output)
        .expect("write object");
        assert_eq!(
            struct_with_union_into_bytes!(flat_binder_object {
                hdr.type_: BINDER_TYPE_BINDER,
                flags: 42,
                cookie: 0xDEADDEAD,
                __bindgen_anon_1.binder: 0xDEADBEEF,
            }),
            output
        );
    }

    #[fuchsia::test]
    fn serialize_binder_fd() {
        let mut output = [0u8; std::mem::size_of::<flat_binder_object>()];

        SerializedBinderObject::File { fd: FdNumber::from_raw(2), flags: 42, cookie: 99 }
            .write_to(&mut output)
            .expect("write fd");
        assert_eq!(
            struct_with_union_into_bytes!(flat_binder_object {
                hdr.type_: BINDER_TYPE_FD,
                flags: 42,
                cookie: 99,
                __bindgen_anon_1.handle: 2,
            }),
            output
        );
    }

    #[fuchsia::test]
    fn serialize_binder_buffer() {
        let mut output = [0u8; std::mem::size_of::<binder_buffer_object>()];

        SerializedBinderObject::Buffer {
            buffer: UserAddress::from(0xDEADBEEF),
            length: 0x100,
            parent: 1,
            parent_offset: 20,
            flags: 42,
        }
        .write_to(&mut output)
        .expect("write buffer");
        assert_eq!(
            binder_buffer_object {
                hdr: binder_object_header { type_: BINDER_TYPE_PTR },
                buffer: 0xDEADBEEF,
                length: 0x100,
                parent: 1,
                parent_offset: 20,
                flags: 42,
            }
            .as_bytes(),
            output
        );
    }

    #[fuchsia::test]
    fn serialize_binder_fd_array() {
        let mut output = [0u8; std::mem::size_of::<binder_fd_array_object>()];

        SerializedBinderObject::FileArray { num_fds: 2, parent: 1, parent_offset: 20 }
            .write_to(&mut output)
            .expect("write fd array");
        assert_eq!(
            binder_fd_array_object {
                hdr: binder_object_header { type_: BINDER_TYPE_FDA },
                num_fds: 2,
                parent: 1,
                parent_offset: 20,
                pad: 0,
            }
            .as_bytes(),
            output
        );
    }

    #[fuchsia::test]
    fn serialize_binder_buffer_too_small() {
        let mut output = [0u8; std::mem::size_of::<binder_uintptr_t>()];
        SerializedBinderObject::Handle { handle: 2.into(), flags: 42, cookie: 99 }
            .write_to(&mut output)
            .expect_err("write handle should not succeed");
        SerializedBinderObject::Object {
            local: LocalBinderObject {
                weak_ref_addr: UserAddress::from(0xDEADBEEF),
                strong_ref_addr: UserAddress::from(0xDEADDEAD),
            },
            flags: 42,
        }
        .write_to(&mut output)
        .expect_err("write object should not succeed");
        SerializedBinderObject::File { fd: FdNumber::from_raw(2), flags: 42, cookie: 99 }
            .write_to(&mut output)
            .expect_err("write fd should not succeed");
    }

    #[fuchsia::test]
    fn deserialize_binder_handle() {
        let input = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 42,
            cookie: 99,
            __bindgen_anon_1.handle: 2,
        });
        assert_eq!(
            SerializedBinderObject::from_bytes(&input).expect("read handle"),
            SerializedBinderObject::Handle { handle: 2.into(), flags: 42, cookie: 99 }
        );
    }

    #[fuchsia::test]
    fn deserialize_binder_object() {
        let input = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_BINDER,
            flags: 42,
            cookie: 0xDEADDEAD,
            __bindgen_anon_1.binder: 0xDEADBEEF,
        });
        assert_eq!(
            SerializedBinderObject::from_bytes(&input).expect("read object"),
            SerializedBinderObject::Object {
                local: LocalBinderObject {
                    weak_ref_addr: UserAddress::from(0xDEADBEEF),
                    strong_ref_addr: UserAddress::from(0xDEADDEAD)
                },
                flags: 42
            }
        );
    }

    #[fuchsia::test]
    fn deserialize_binder_fd() {
        let input = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_FD,
            flags: 42,
            cookie: 99,
            __bindgen_anon_1.handle: 2,
        });
        assert_eq!(
            SerializedBinderObject::from_bytes(&input).expect("read handle"),
            SerializedBinderObject::File { fd: FdNumber::from_raw(2), flags: 42, cookie: 99 }
        );
    }

    #[fuchsia::test]
    fn deserialize_binder_buffer() {
        let input = binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: 0xDEADBEEF,
            length: 0x100,
            parent: 1,
            parent_offset: 20,
            flags: 42,
        };
        assert_eq!(
            SerializedBinderObject::from_bytes(input.as_bytes()).expect("read buffer"),
            SerializedBinderObject::Buffer {
                buffer: UserAddress::from(0xDEADBEEF),
                length: 0x100,
                parent: 1,
                parent_offset: 20,
                flags: 42
            }
        );
    }

    #[fuchsia::test]
    fn deserialize_binder_fd_array() {
        let input = binder_fd_array_object {
            hdr: binder_object_header { type_: BINDER_TYPE_FDA },
            num_fds: 2,
            pad: 0,
            parent: 1,
            parent_offset: 20,
        };
        assert_eq!(
            SerializedBinderObject::from_bytes(input.as_bytes()).expect("read fd array"),
            SerializedBinderObject::FileArray { num_fds: 2, parent: 1, parent_offset: 20 }
        );
    }

    #[fuchsia::test]
    fn deserialize_unknown_object() {
        let input = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: 9001,
            flags: 42,
            cookie: 99,
            __bindgen_anon_1.handle: 2,
        });
        SerializedBinderObject::from_bytes(&input).expect_err("read unknown object");
    }

    #[fuchsia::test]
    fn deserialize_input_too_small() {
        let input = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_FD,
            flags: 42,
            cookie: 99,
            __bindgen_anon_1.handle: 2,
        });
        SerializedBinderObject::from_bytes(&input[..std::mem::size_of::<binder_uintptr_t>()])
            .expect_err("read buffer too small");
    }

    #[fuchsia::test]
    fn deserialize_unaligned() {
        let input = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 42,
            cookie: 99,
            __bindgen_anon_1.handle: 2,
        });
        let mut unaligned_input = Vec::new();
        unaligned_input.push(0u8);
        unaligned_input.extend(input);
        SerializedBinderObject::from_bytes(&unaligned_input[1..]).expect("read unaligned object");
    }

    struct TranslateHandlesTestFixture {
        _kernel: Arc<Kernel>,
        driver: Arc<BinderDriver>,

        sender_task: CurrentTask,
        sender_proc: Arc<BinderProcess>,
        sender_thread: Arc<BinderThread>,

        receiver_task: CurrentTask,
        receiver_proc: Arc<BinderProcess>,
    }

    impl TranslateHandlesTestFixture {
        fn new() -> Self {
            let (kernel, sender_task) = create_kernel_and_task();
            let driver = BinderDriver::new();
            let (sender_proc, sender_thread) = driver.create_process_and_thread(sender_task.id);
            let receiver_task = create_task(&kernel, "receiver_task");
            let receiver_proc = driver.create_process(receiver_task.id);

            mmap_shared_memory(&driver, &sender_task, &sender_proc);
            mmap_shared_memory(&driver, &receiver_task, &receiver_proc);

            Self {
                _kernel: kernel,
                driver,
                sender_task,
                sender_proc,
                sender_thread,
                receiver_task,
                receiver_proc,
            }
        }

        fn lock_receiver_shared_memory<'a>(
            &'a self,
        ) -> crate::lock::MappedMutexGuard<'a, SharedMemory> {
            crate::lock::MutexGuard::map(self.receiver_proc.shared_memory.lock(), |value| {
                value.as_mut().unwrap()
            })
        }
    }

    #[fuchsia::test]
    fn copy_transaction_data_between_processes() {
        let test = TranslateHandlesTestFixture::new();

        // Explicitly install a VMO that we can read from later.
        let vmo = zx::Vmo::create(VMO_LENGTH as u64).expect("failed to create VMO");
        *test.receiver_proc.shared_memory.lock() = Some(
            SharedMemory::map(&vmo, BASE_ADDR, VMO_LENGTH).expect("failed to map shared memory"),
        );

        // Map some memory for process 1.
        let data_addr = map_memory(&test.sender_task, UserAddress::default(), *PAGE_SIZE);

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
            + test
                .sender_task
                .mm
                .write_memory(data_addr, &transaction_data)
                .expect("failed to write transaction data");

        // Write the offsets data (where in the data buffer `flat_binder_object`s are).
        let offsets_data: u64 = BINDER_DATA.len() as u64;
        test.sender_task
            .mm
            .write_object(UserRef::new(offsets_addr), &offsets_data)
            .expect("failed to write offsets buffer");

        // Construct the `binder_transaction_data` struct that contains pointers to the data and
        // offsets buffers.
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: 1,
                flags: 0,
                sender_pid: test.sender_proc.pid,
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
            },
            buffers_size: 0,
        };

        // Copy the data from process 1 to process 2
        let (data_buffer, offsets_buffer, _transaction_state) = test
            .driver
            .copy_transaction_buffers(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &transaction,
            )
            .expect("copy data");

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

    #[fuchsia::test]
    fn transaction_translate_binder_leaving_process() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        const BINDER_OBJECT: LocalBinderObject = LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000010),
            strong_ref_addr: UserAddress::from(0x0000000000000100),
        };

        const DATA_PREAMBLE: &[u8; 5] = b"stuff";

        let mut transaction_data = Vec::new();
        transaction_data.extend(DATA_PREAMBLE);
        let offsets = [transaction_data.len() as binder_uintptr_t];
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_BINDER,
            flags: 0,
            cookie: BINDER_OBJECT.strong_ref_addr.ptr() as u64,
            __bindgen_anon_1.binder: BINDER_OBJECT.weak_ref_addr.ptr() as u64,
        }));

        const EXPECTED_HANDLE: Handle = Handle::from_raw(1);

        let transaction_state = test
            .driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &offsets,
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect("failed to translate handles");

        // Verify that the new handle was returned in `transaction_state` so that it gets dropped
        // at the end of the transaction.
        assert_eq!(transaction_state.state.handles[0], EXPECTED_HANDLE);

        // Verify that the transaction data was mutated.
        let mut expected_transaction_data = Vec::new();
        expected_transaction_data.extend(DATA_PREAMBLE);
        expected_transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: EXPECTED_HANDLE.into(),
        }));
        assert_eq!(&expected_transaction_data, &transaction_data);

        // Verify that a handle was created in the receiver.
        let object = test
            .receiver_proc
            .handles
            .lock()
            .get(EXPECTED_HANDLE.object_index())
            .expect("expected handle not present");
        assert!(std::ptr::eq(object.owner.as_ptr(), Arc::as_ptr(&test.sender_proc)));
        assert_eq!(object.local, BINDER_OBJECT);

        // Verify that a strong acquire command is sent to the sender process (on the same thread
        // that sent the transaction).
        assert_matches!(
            test.sender_thread.read().command_queue.front(),
            Some(Command::AcquireRef(BINDER_OBJECT))
        );
    }

    #[fuchsia::test]
    fn transaction_translate_binder_handle_entering_owning_process() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        let binder_object = LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000010),
            strong_ref_addr: UserAddress::from(0x0000000000000100),
        };

        // Pretend the binder object was given to the sender earlier, so it can be sent back.
        let handle = test.sender_proc.handles.lock().insert_for_transaction(Arc::new(
            BinderObject::new(&test.receiver_proc, binder_object),
        ));

        const DATA_PREAMBLE: &[u8; 5] = b"stuff";

        let mut transaction_data = Vec::new();
        transaction_data.extend(DATA_PREAMBLE);
        let offsets = [transaction_data.len() as binder_uintptr_t];
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: handle.into(),
        }));

        test.driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &offsets,
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect("failed to translate handles");

        // Verify that the transaction data was mutated.
        let mut expected_transaction_data = Vec::new();
        expected_transaction_data.extend(DATA_PREAMBLE);
        expected_transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_BINDER,
            flags: 0,
            cookie: binder_object.strong_ref_addr.ptr() as u64,
            __bindgen_anon_1.binder: binder_object.weak_ref_addr.ptr() as u64,
        }));
        assert_eq!(&expected_transaction_data, &transaction_data);
    }

    #[fuchsia::test]
    fn transaction_translate_binder_handle_passed_between_non_owning_processes() {
        let test = TranslateHandlesTestFixture::new();
        let owner_proc = test.driver.create_process(3);
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        let binder_object = LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000010),
            strong_ref_addr: UserAddress::from(0x0000000000000100),
        };

        const SENDING_HANDLE: Handle = Handle::from_raw(1);
        const RECEIVING_HANDLE: Handle = Handle::from_raw(2);

        // Pretend the binder object was given to the sender earlier.
        let handle = test
            .sender_proc
            .handles
            .lock()
            .insert_for_transaction(Arc::new(BinderObject::new(&owner_proc, binder_object)));
        assert_eq!(SENDING_HANDLE, handle);

        // Give the receiver another handle so that the input handle number and output handle
        // number aren't the same.
        test.receiver_proc.handles.lock().insert_for_transaction(Arc::new(BinderObject::new(
            &owner_proc,
            LocalBinderObject::default(),
        )));

        const DATA_PREAMBLE: &[u8; 5] = b"stuff";

        let mut transaction_data = Vec::new();
        transaction_data.extend(DATA_PREAMBLE);
        let offsets = [transaction_data.len() as binder_uintptr_t];
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: SENDING_HANDLE.into(),
        }));

        let transaction_state = test
            .driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &offsets,
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect("failed to translate handles");

        // Verify that the new handle was returned in `transaction_state` so that it gets dropped
        // at the end of the transaction.
        assert_eq!(transaction_state.state.handles[0], RECEIVING_HANDLE);

        // Verify that the transaction data was mutated.
        let mut expected_transaction_data = Vec::new();
        expected_transaction_data.extend(DATA_PREAMBLE);
        expected_transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: RECEIVING_HANDLE.into(),
        }));
        assert_eq!(&expected_transaction_data, &transaction_data);

        // Verify that a handle was created in the receiver.
        let object = test
            .receiver_proc
            .handles
            .lock()
            .get(RECEIVING_HANDLE.object_index())
            .expect("expected handle not present");
        assert!(std::ptr::eq(object.owner.as_ptr(), Arc::as_ptr(&owner_proc)));
        assert_eq!(object.local, binder_object);
    }

    /// Tests that hwbinder's scatter-gather buffer-fix-up implementation is correct.
    #[fuchsia::test]
    fn transaction_translate_buffers() {
        let test = TranslateHandlesTestFixture::new();

        // Allocate memory in the sender to hold all the buffers that will get submitted to the
        // binder driver.
        let sender_addr = map_memory(&test.sender_task, UserAddress::default(), *PAGE_SIZE);
        let mut writer = UserMemoryWriter::new(&test.sender_task, sender_addr);

        // Serialize a string into memory.
        const FOO_STR_LEN: i32 = 3;
        const FOO_STR_PADDED_LEN: u64 = 8;
        let sender_foo_addr = writer.write(b"foo");

        // Pad the next buffer to ensure 8-byte alignment.
        writer.write(&[0; FOO_STR_PADDED_LEN as usize - FOO_STR_LEN as usize]);

        // Serialize a C struct that points to the above string.
        #[repr(C)]
        #[derive(AsBytes)]
        struct Bar {
            foo_str: UserAddress,
            len: i32,
            _padding: u32,
        }
        let sender_bar_addr =
            writer.write_object(&Bar { foo_str: sender_foo_addr, len: FOO_STR_LEN, _padding: 0 });

        // Mark the start of the transaction data.
        let transaction_data_addr = writer.current_address();

        // Write the buffer object representing the C struct `Bar`.
        let sender_buffer0_addr = writer.write_object(&binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: sender_bar_addr.ptr() as u64,
            length: std::mem::size_of::<Bar>() as u64,
            ..binder_buffer_object::default()
        });

        // Write the buffer object representing the "foo" string. Its parent is the C struct `Bar`,
        // which has a pointer to it.
        let sender_buffer1_addr = writer.write_object(&binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: sender_foo_addr.ptr() as u64,
            length: FOO_STR_LEN as u64,
            // Mark this buffer as having a parent who references it. The driver will then read
            // the next two fields.
            flags: BINDER_BUFFER_FLAG_HAS_PARENT,
            // The index in the offsets array of the parent buffer.
            parent: 0,
            // The location in the parent buffer where a pointer to this object needs to be
            // fixed up.
            parent_offset: offset_of!(Bar, foo_str) as u64,
        });

        // Write the offsets array.
        let offsets_addr = writer.current_address();
        writer.write_object(&((sender_buffer0_addr - transaction_data_addr) as u64));
        writer.write_object(&((sender_buffer1_addr - transaction_data_addr) as u64));

        let end_data_addr = writer.current_address();

        // Construct the input for the binder driver to process.
        let input = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                target: binder_transaction_data__bindgen_ty_1 { handle: 1 },
                data_size: (offsets_addr - transaction_data_addr) as u64,
                offsets_size: (end_data_addr - offsets_addr) as u64,
                data: binder_transaction_data__bindgen_ty_2 {
                    ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                        buffer: transaction_data_addr.ptr() as u64,
                        offsets: offsets_addr.ptr() as u64,
                    },
                },
                ..binder_transaction_data::new_zeroed()
            },
            // Each buffer size must be rounded up to a multiple of 8 to ensure enough
            // space in the allocated target for 8-byte alignment.
            buffers_size: std::mem::size_of::<Bar>() as u64 + FOO_STR_PADDED_LEN,
        };

        // Perform the translation and copying.
        let (data_buffer, _, _) = test
            .driver
            .copy_transaction_buffers(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &input,
            )
            .expect("copy_transaction_buffers");

        // Read back the translated objects from the receiver's memory.
        let mut translated_objects = [binder_buffer_object::default(); 2];
        test.receiver_task
            .mm
            .read_objects(UserRef::new(data_buffer.address), &mut translated_objects)
            .expect("read output");

        // Check that the second buffer is the string "foo".
        let foo_addr = UserAddress::from(translated_objects[1].buffer);
        let mut str = [0u8; 3];
        test.receiver_task.mm.read_memory(foo_addr, &mut str).expect("read buffer 1");
        assert_eq!(&str, b"foo");

        // Check that the first buffer points to the string "foo".
        let foo_ptr: UserAddress = test
            .receiver_task
            .mm
            .read_object(UserRef::new(UserAddress::from(translated_objects[0].buffer)))
            .expect("read buffer 0");
        assert_eq!(foo_ptr, foo_addr);
    }

    /// Tests that when the scatter-gather buffer size reported by userspace is too small, we stop
    /// processing and fail, instead of skipping a buffer object that doesn't fit.
    #[fuchsia::test]
    fn transaction_fails_when_sg_buffer_size_is_too_small() {
        let test = TranslateHandlesTestFixture::new();

        // Allocate memory in the sender to hold all the buffers that will get submitted to the
        // binder driver.
        let sender_addr = map_memory(&test.sender_task, UserAddress::default(), *PAGE_SIZE);
        let mut writer = UserMemoryWriter::new(&test.sender_task, sender_addr);

        // Serialize a series of buffers that point to empty data. Each successive buffer is smaller
        // than the last.
        let buffer_objects = [8, 7, 6]
            .iter()
            .map(|size| binder_buffer_object {
                hdr: binder_object_header { type_: BINDER_TYPE_PTR },
                buffer: writer
                    .write(&{
                        let mut data = Vec::new();
                        data.resize(*size, 0u8);
                        data
                    })
                    .ptr() as u64,
                length: *size as u64,
                ..binder_buffer_object::default()
            })
            .collect::<Vec<_>>();

        // Mark the start of the transaction data.
        let transaction_data_addr = writer.current_address();

        // Write the buffer objects to the transaction payload.
        let offsets = buffer_objects
            .into_iter()
            .map(|buffer_object| {
                (writer.write_object(&buffer_object) - transaction_data_addr) as u64
            })
            .collect::<Vec<_>>();

        // Write the offsets array.
        let offsets_addr = writer.current_address();
        for offset in offsets {
            writer.write_object(&offset);
        }

        let end_data_addr = writer.current_address();

        // Construct the input for the binder driver to process.
        let input = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                target: binder_transaction_data__bindgen_ty_1 { handle: 1 },
                data_size: (offsets_addr - transaction_data_addr) as u64,
                offsets_size: (end_data_addr - offsets_addr) as u64,
                data: binder_transaction_data__bindgen_ty_2 {
                    ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                        buffer: transaction_data_addr.ptr() as u64,
                        offsets: offsets_addr.ptr() as u64,
                    },
                },
                ..binder_transaction_data::new_zeroed()
            },
            // Make the buffers size only fit the first buffer fully (size 8). The remaining space
            // should be 6 bytes, so that the second buffer doesn't fit but the next one does.
            buffers_size: 8 + 6,
        };

        // Perform the translation and copying.
        test.driver
            .copy_transaction_buffers(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &input,
            )
            .expect_err("copy_transaction_buffers should fail");
    }

    /// Tests that when a scatter-gather buffer refers to a parent that comes *after* it in the
    /// object list, the transaction fails.
    #[fuchsia::test]
    fn transaction_fails_when_sg_buffer_parent_is_out_of_order() {
        let test = TranslateHandlesTestFixture::new();

        // Allocate memory in the sender to hold all the buffers that will get submitted to the
        // binder driver.
        let sender_addr = map_memory(&test.sender_task, UserAddress::default(), *PAGE_SIZE);
        let mut writer = UserMemoryWriter::new(&test.sender_task, sender_addr);

        // Write the data for two buffer objects.
        const BUFFER_DATA_LEN: usize = 8;
        let buf0_addr = writer.write(&[0; BUFFER_DATA_LEN]);
        let buf1_addr = writer.write(&[0; BUFFER_DATA_LEN]);

        // Mark the start of the transaction data.
        let transaction_data_addr = writer.current_address();

        // Write a buffer object that marks a future buffer as its parent.
        let sender_buffer0_addr = writer.write_object(&binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: buf0_addr.ptr() as u64,
            length: BUFFER_DATA_LEN as u64,
            // Mark this buffer as having a parent who references it. The driver will then read
            // the next two fields.
            flags: BINDER_BUFFER_FLAG_HAS_PARENT,
            parent: 0,
            parent_offset: 0,
        });

        // Write a buffer object that acts as the first buffers parent (contains a pointer to the
        // first buffer).
        let sender_buffer1_addr = writer.write_object(&binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: buf1_addr.ptr() as u64,
            length: BUFFER_DATA_LEN as u64,
            ..binder_buffer_object::default()
        });

        // Write the offsets array.
        let offsets_addr = writer.current_address();
        writer.write_object(&((sender_buffer0_addr - transaction_data_addr) as u64));
        writer.write_object(&((sender_buffer1_addr - transaction_data_addr) as u64));

        let end_data_addr = writer.current_address();

        // Construct the input for the binder driver to process.
        let input = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                target: binder_transaction_data__bindgen_ty_1 { handle: 1 },
                data_size: (offsets_addr - transaction_data_addr) as u64,
                offsets_size: (end_data_addr - offsets_addr) as u64,
                data: binder_transaction_data__bindgen_ty_2 {
                    ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                        buffer: transaction_data_addr.ptr() as u64,
                        offsets: offsets_addr.ptr() as u64,
                    },
                },
                ..binder_transaction_data::new_zeroed()
            },
            buffers_size: BUFFER_DATA_LEN as u64 * 2,
        };

        // Perform the translation and copying.
        test.driver
            .copy_transaction_buffers(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &input,
            )
            .expect_err("copy_transaction_buffers should fail");
    }

    #[fuchsia::test]
    fn transaction_translate_fd_array() {
        let test = TranslateHandlesTestFixture::new();

        // Open a file in the sender process that we won't be using. It is there to occupy a file
        // descriptor so that the translation doesn't happen to use the same FDs for receiver and
        // sender, potentially hiding a bug.
        test.sender_task
            .files
            .add_with_flags(PanickingFile::new_file(&test.sender_task), FdFlags::empty())
            .unwrap();

        // Open two files in the sender process. These will be sent in the transaction.
        let files = [
            PanickingFile::new_file(&test.sender_task),
            PanickingFile::new_file(&test.sender_task),
        ];
        let sender_fds = files
            .iter()
            .map(|file| {
                test.sender_task
                    .files
                    .add_with_flags(file.clone(), FdFlags::CLOEXEC)
                    .expect("add file")
            })
            .collect::<Vec<_>>();

        // Ensure that the receiver task has no file descriptors.
        assert!(test.receiver_task.files.get_all_fds().is_empty(), "receiver already has files");

        // Allocate memory in the sender to hold all the buffers that will get submitted to the
        // binder driver.
        let sender_addr = map_memory(&test.sender_task, UserAddress::default(), *PAGE_SIZE);
        let mut writer = UserMemoryWriter::new(&test.sender_task, sender_addr);

        // Serialize a simple buffer. This will ensure that the FD array being translated is not at
        // the beginning of the buffer, exercising the offset math.
        let sender_padding_addr = writer.write(&[0; 8]);

        // Serialize a C struct with an fd array.
        #[repr(C)]
        #[derive(AsBytes, FromBytes)]
        struct Bar {
            len: u32,
            fds: [u32; 2],
            _padding: u32,
        }
        let sender_bar_addr = writer.write_object(&Bar {
            len: 2,
            fds: [sender_fds[0].raw() as u32, sender_fds[1].raw() as u32],
            _padding: 0,
        });

        // Mark the start of the transaction data.
        let transaction_data_addr = writer.current_address();

        // Write the buffer object representing the padding.
        let sender_padding_buffer_addr = writer.write_object(&binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: sender_padding_addr.ptr() as u64,
            length: 8,
            ..binder_buffer_object::default()
        });

        // Write the buffer object representing the C struct `Bar`.
        let sender_buffer_addr = writer.write_object(&binder_buffer_object {
            hdr: binder_object_header { type_: BINDER_TYPE_PTR },
            buffer: sender_bar_addr.ptr() as u64,
            length: std::mem::size_of::<Bar>() as u64,
            ..binder_buffer_object::default()
        });

        // Write the fd array object that tells the kernel where the file descriptors are in the
        // `Bar` buffer.
        let sender_fd_array_addr = writer.write_object(&binder_fd_array_object {
            hdr: binder_object_header { type_: BINDER_TYPE_FDA },
            pad: 0,
            num_fds: sender_fds.len() as u64,
            // The index in the offsets array of the parent buffer.
            parent: 1,
            // The location in the parent buffer where the FDs are, which need to be duped.
            parent_offset: offset_of!(Bar, fds) as u64,
        });

        // Write the offsets array.
        let offsets_addr = writer.current_address();
        writer.write_object(&((sender_padding_buffer_addr - transaction_data_addr) as u64));
        writer.write_object(&((sender_buffer_addr - transaction_data_addr) as u64));
        writer.write_object(&((sender_fd_array_addr - transaction_data_addr) as u64));

        let end_data_addr = writer.current_address();

        // Construct the input for the binder driver to process.
        let input = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                target: binder_transaction_data__bindgen_ty_1 { handle: 1 },
                data_size: (offsets_addr - transaction_data_addr) as u64,
                offsets_size: (end_data_addr - offsets_addr) as u64,
                data: binder_transaction_data__bindgen_ty_2 {
                    ptr: binder_transaction_data__bindgen_ty_2__bindgen_ty_1 {
                        buffer: transaction_data_addr.ptr() as u64,
                        offsets: offsets_addr.ptr() as u64,
                    },
                },
                ..binder_transaction_data::new_zeroed()
            },
            buffers_size: std::mem::size_of::<Bar>() as u64 + 8,
        };

        // Perform the translation and copying.
        let (data_buffer, _, transient_transaction_state) = test
            .driver
            .copy_transaction_buffers(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &input,
            )
            .expect("copy_transaction_buffers");

        // Simulate a successful transaction by converting the transient state.
        let _transaction_state: TransactionState = transient_transaction_state.into();

        // Start reading from the receiver's memory, which holds the translated transaction.
        let mut reader = UserMemoryCursor::new(
            &test.receiver_task.mm,
            data_buffer.address,
            data_buffer.length as u64,
        );

        // Skip the first object, it was only there to pad the next one.
        reader.read_object::<binder_buffer_object>().expect("read padding buffer");

        // Read back the buffer object representing `Bar`.
        let bar_buffer_object =
            reader.read_object::<binder_buffer_object>().expect("read bar buffer object");
        let translated_bar = test
            .receiver_task
            .mm
            .read_object::<Bar>(UserRef::new(UserAddress::from(bar_buffer_object.buffer)))
            .expect("read Bar");

        // Verify that the fds have been translated.
        let (receiver_file, receiver_fd_flags) = test
            .receiver_task
            .files
            .get_with_flags(FdNumber::from_raw(translated_bar.fds[0] as i32))
            .expect("FD not found in receiver");
        assert!(
            Arc::ptr_eq(&receiver_file, &files[0]),
            "FD in receiver does not refer to the same file as sender"
        );
        assert_eq!(receiver_fd_flags, FdFlags::CLOEXEC);
        let (receiver_file, receiver_fd_flags) = test
            .receiver_task
            .files
            .get_with_flags(FdNumber::from_raw(translated_bar.fds[1] as i32))
            .expect("FD not found in receiver");
        assert!(
            Arc::ptr_eq(&receiver_file, &files[1]),
            "FD in receiver does not refer to the same file as sender"
        );
        assert_eq!(receiver_fd_flags, FdFlags::CLOEXEC);
    }

    #[fuchsia::test]
    fn transaction_translation_fails_on_invalid_handle() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        let mut transaction_data = Vec::new();
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: 42,
        }));

        let transaction_ref_error = test
            .driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &[0 as binder_uintptr_t],
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect_err("translate handles unexpectedly succeeded");

        assert_eq!(transaction_ref_error, TransactionError::Failure);
    }

    #[fuchsia::test]
    fn transaction_translation_fails_on_invalid_object_type() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        let mut transaction_data = Vec::new();
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_WEAK_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: 42,
        }));

        let transaction_ref_error = test
            .driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &[0 as binder_uintptr_t],
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect_err("translate handles unexpectedly succeeded");

        assert_eq!(transaction_ref_error, TransactionError::Malformed(errno!(EINVAL)));
    }

    #[fuchsia::test]
    fn transaction_drop_references_on_failed_transaction() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        let binder_object = LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000010),
            strong_ref_addr: UserAddress::from(0x0000000000000100),
        };

        let mut transaction_data = Vec::new();
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_BINDER,
            flags: 0,
            cookie: binder_object.strong_ref_addr.ptr() as u64,
            __bindgen_anon_1.binder: binder_object.weak_ref_addr.ptr() as u64,
        }));
        transaction_data.extend(struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_HANDLE,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: 42,
        }));

        test.driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &[
                    0 as binder_uintptr_t,
                    std::mem::size_of::<flat_binder_object>() as binder_uintptr_t,
                ],
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect_err("translate handles unexpectedly succeeded");

        // Ensure that the handle created in the receiving process is not present.
        assert!(
            test.receiver_proc.handles.lock().get(0).is_none(),
            "handle present when it should have been dropped"
        );
    }

    #[fuchsia::test]
    fn process_state_cleaned_up_after_binder_fd_closed() {
        let (_kernel, current_task) = create_kernel_and_task();
        let binder_driver = BinderDriver::new();
        let node = FsNode::new_root(PanickingFsNode);

        // Open the binder device, which creates an instance of the binder device associated with
        // the process.
        let binder_instance = binder_driver
            .open(&current_task, DeviceType::NONE, &node, OpenFlags::RDWR)
            .expect("binder dev open failed");

        // Ensure that the binder driver has created process state.
        binder_driver.find_process(current_task.get_pid()).expect("failed to find process");

        // Simulate closing the FD by dropping the binder instance.
        drop(binder_instance);

        // Verify that the process state no longer exists.
        binder_driver.find_process(current_task.get_pid()).expect_err("process was not cleaned up");
    }

    #[fuchsia::test]
    fn decrementing_refs_on_dead_binder_succeeds() {
        let driver = BinderDriver::new();

        let owner_proc = driver.create_process(1);
        let client_proc = driver.create_process(2);

        // Register an object with the owner.
        let object = owner_proc.find_or_register_object(LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000001),
            strong_ref_addr: UserAddress::from(0x0000000000000002),
        });

        // Keep a weak reference to the object.
        let weak_object = Arc::downgrade(&object);

        // Insert a handle to the object in the client. This also retains a strong reference.
        let handle = client_proc.handles.lock().insert_for_transaction(object);

        // Grab a weak reference.
        client_proc.handles.lock().inc_weak(handle.object_index()).expect("inc_weak");

        // Now the owner process dies.
        driver.procs.write().remove(&owner_proc.pid);
        drop(owner_proc);

        // Confirm that the object is considered dead. The representation is still alive, but the
        // owner is dead.
        assert!(
            client_proc
                .handles
                .lock()
                .get(handle.object_index())
                .expect("object should be present")
                .owner
                .upgrade()
                .is_none(),
            "owner should be dead"
        );

        // Decrement the weak reference. This should prove that the handle is still occupied.
        client_proc.handles.lock().dec_weak(handle.object_index()).expect("dec_weak");

        // Decrement the last strong reference.
        client_proc.handles.lock().dec_strong(handle.object_index()).expect("dec_strong");

        // Confirm that now the handle has been removed from the table.
        assert!(
            client_proc.handles.lock().get(handle.object_index()).is_none(),
            "handle should have been dropped"
        );

        // Now the binder object representation should also be gone.
        assert!(weak_object.upgrade().is_none(), "object should be dead");
    }

    #[fuchsia::test]
    fn death_notification_fires_when_process_dies() {
        let (_kernel, current_task) = create_kernel_and_task();
        let driver = BinderDriver::new();

        let owner_proc = driver.create_process(1);
        let (client_proc, client_thread) = driver.create_process_and_thread(2);

        // Register an object with the owner.
        let object = owner_proc.find_or_register_object(LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000001),
            strong_ref_addr: UserAddress::from(0x0000000000000002),
        });

        // Insert a handle to the object in the client. This also retains a strong reference.
        let handle = client_proc.handles.lock().insert_for_transaction(object);

        const DEATH_NOTIFICATION_COOKIE: binder_uintptr_t = 0xDEADBEEF;

        // Register a death notification handler.
        driver
            .handle_request_death_notification(
                &current_task,
                &client_proc,
                &client_thread,
                handle,
                DEATH_NOTIFICATION_COOKIE,
            )
            .expect("request death notification");

        // Pretend the client thread is waiting for commands, so that it can be scheduled commands.
        let fake_waiter = Waiter::new();
        {
            let mut client_state = client_thread.write();
            client_state.registration = RegistrationState::MAIN;
            client_state.waiter = fake_waiter.weak();
        }

        // Now the owner process dies.
        driver.procs.write().remove(&owner_proc.pid);
        drop(owner_proc);

        // The client thread should have a notification waiting.
        assert_matches!(
            client_thread.read().command_queue.front(),
            Some(Command::DeadBinder(DEATH_NOTIFICATION_COOKIE))
        );
    }

    #[fuchsia::test]
    fn death_notification_fires_when_request_for_death_notification_is_made_on_dead_binder() {
        let (_kernel, current_task) = create_kernel_and_task();
        let driver = BinderDriver::new();

        let owner_proc = driver.create_process(1);
        let (client_proc, client_thread) = driver.create_process_and_thread(2);

        // Register an object with the owner.
        let object = owner_proc.find_or_register_object(LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000001),
            strong_ref_addr: UserAddress::from(0x0000000000000002),
        });

        // Insert a handle to the object in the client. This also retains a strong reference.
        let handle = client_proc.handles.lock().insert_for_transaction(object);

        // Now the owner process dies.
        driver.procs.write().remove(&owner_proc.pid);
        drop(owner_proc);

        const DEATH_NOTIFICATION_COOKIE: binder_uintptr_t = 0xDEADBEEF;

        // Register a death notification handler.
        driver
            .handle_request_death_notification(
                &current_task,
                &client_proc,
                &client_thread,
                handle,
                DEATH_NOTIFICATION_COOKIE,
            )
            .expect("request death notification");

        // The client thread should not have a notification, as the calling thread is not allowed
        // to receive it, or else a deadlock may occur if the thread is in the middle of a
        // transaction. Since there is only one thread, check the process command queue.
        assert_matches!(
            client_proc.command_queue.lock().front(),
            Some(Command::DeadBinder(DEATH_NOTIFICATION_COOKIE))
        );
    }

    #[fuchsia::test]
    fn death_notification_is_cleared_before_process_dies() {
        let (_kernel, current_task) = create_kernel_and_task();
        let driver = BinderDriver::new();

        let owner_proc = driver.create_process(1);
        let (client_proc, client_thread) = driver.create_process_and_thread(2);

        // Register an object with the owner.
        let object = owner_proc.find_or_register_object(LocalBinderObject {
            weak_ref_addr: UserAddress::from(0x0000000000000001),
            strong_ref_addr: UserAddress::from(0x0000000000000002),
        });

        // Insert a handle to the object in the client. This also retains a strong reference.
        let handle = client_proc.handles.lock().insert_for_transaction(object);

        let death_notification_cookie = 0xDEADBEEF;

        // Register a death notification handler.
        driver
            .handle_request_death_notification(
                &current_task,
                &client_proc,
                &client_thread,
                handle,
                death_notification_cookie,
            )
            .expect("request death notification");

        // Now clear the death notification handler.
        driver
            .handle_clear_death_notification(
                &current_task,
                &client_proc,
                handle,
                death_notification_cookie,
            )
            .expect("clear death notification");

        // Pretend the client thread is waiting for commands, so that it can be scheduled commands.
        let fake_waiter = Waiter::new();
        {
            let mut client_state = client_thread.write();
            client_state.registration = RegistrationState::MAIN;
            client_state.waiter = fake_waiter.weak();
        }

        // Now the owner process dies.
        driver.procs.write().remove(&owner_proc.pid);
        drop(owner_proc);

        // The client thread should have no notification.
        assert!(client_thread.read().command_queue.is_empty());

        // The client process should have no notification.
        assert!(client_proc.command_queue.lock().is_empty());
    }

    #[fuchsia::test]
    fn send_fd_in_transaction() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        // Open a file in the sender process.
        let file = PanickingFile::new_file(&test.sender_task);
        let sender_fd = test
            .sender_task
            .files
            .add_with_flags(file.clone(), FdFlags::CLOEXEC)
            .expect("add file");

        // Send the fd in a transaction. `flags` and `cookie` are set so that we can ensure binder
        // driver doesn't touch them/passes them through.
        let mut transaction_data = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_FD,
            flags: 42,
            cookie: 51,
            __bindgen_anon_1.handle: sender_fd.raw() as u32,
        });
        let offsets = [0];

        let transient_transaction_state = test
            .driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &offsets,
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect("failed to translate handles");

        // Simulate success by converting the transient state.
        let _transaction_state: TransactionState = transient_transaction_state.into();

        // The receiver should now have a file.
        let receiver_fd = test
            .receiver_task
            .files
            .get_all_fds()
            .first()
            .cloned()
            .expect("receiver should have FD");

        // The FD should have the same flags.
        assert_eq!(
            test.receiver_task.files.get_fd_flags(receiver_fd).expect("get flags"),
            FdFlags::CLOEXEC
        );

        // The FD should point to the same file.
        assert!(
            Arc::ptr_eq(
                &test.receiver_task.files.get(receiver_fd).expect("receiver should have FD"),
                &file
            ),
            "FDs from sender and receiver don't point to the same file"
        );

        let expected_transaction_data = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_FD,
            flags: 42,
            cookie: 51,
            __bindgen_anon_1.handle: receiver_fd.raw() as u32,
        });

        assert_eq!(expected_transaction_data, transaction_data);
    }

    #[fuchsia::test]
    fn cleanup_fd_in_failed_transaction() {
        let test = TranslateHandlesTestFixture::new();
        let mut receiver_shared_memory = test.lock_receiver_shared_memory();
        let (_, _, mut sg_buffer) =
            receiver_shared_memory.allocate_buffers(0, 0, 0).expect("allocate buffers");

        // Open a file in the sender process.
        let sender_fd = test
            .sender_task
            .files
            .add_with_flags(PanickingFile::new_file(&test.sender_task), FdFlags::CLOEXEC)
            .expect("add file");

        // Send the fd in a transaction.
        let mut transaction_data = struct_with_union_into_bytes!(flat_binder_object {
            hdr.type_: BINDER_TYPE_FD,
            flags: 0,
            cookie: 0,
            __bindgen_anon_1.handle: sender_fd.raw() as u32,
        });
        let offsets = [0];

        let transaction_state = test
            .driver
            .translate_handles(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                &test.receiver_task,
                &test.receiver_proc,
                &offsets,
                &mut transaction_data,
                &mut sg_buffer,
            )
            .expect("failed to translate handles");

        assert!(!test.receiver_task.files.get_all_fds().is_empty(), "receiver should have a file");

        // Simulate an error, which will drop the transaction state.
        drop(transaction_state);

        assert!(
            test.receiver_task.files.get_all_fds().is_empty(),
            "receiver should not have any files"
        );
    }

    #[fuchsia::test]
    fn transaction_error_dispatch() {
        let driver = BinderDriver::new();
        let (_proc, thread) = driver.create_process_and_thread(1);

        TransactionError::Malformed(errno!(EINVAL)).dispatch(&thread).expect("no error");
        assert_matches!(
            thread.write().command_queue.pop_front(),
            Some(Command::Error(val)) if val == EINVAL.return_value() as i32
        );

        TransactionError::Failure.dispatch(&thread).expect("no error");
        assert_matches!(thread.write().command_queue.pop_front(), Some(Command::FailedReply));

        TransactionError::Dead.dispatch(&thread).expect("no error");
        assert_matches!(thread.write().command_queue.pop_front(), Some(Command::DeadReply));
    }

    #[fuchsia::test]
    fn next_oneway_transaction_scheduled_after_buffer_freed() {
        let (kernel, sender_task) = create_kernel_and_task();
        let receiver_task = create_task(&kernel, "test-task2");

        let driver = BinderDriver::new();
        let (sender_proc, sender_thread) = driver.create_process_and_thread(sender_task.id);
        let (receiver_proc, _receiver_thread) = driver.create_process_and_thread(receiver_task.id);

        // Initialize the receiver process with shared memory in the driver.
        mmap_shared_memory(&driver, &receiver_task, &receiver_proc);

        // Insert a binder object for the receiver, and grab a handle to it in the sender.
        const OBJECT_ADDR: UserAddress = UserAddress::from(0x01);
        let object = register_binder_object(&receiver_proc, OBJECT_ADDR, OBJECT_ADDR + 1u64);
        let handle = sender_proc.handles.lock().insert_for_transaction(object.clone());

        // Construct a oneway transaction to send from the sender to the receiver.
        const FIRST_TRANSACTION_CODE: u32 = 42;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: FIRST_TRANSACTION_CODE,
                flags: transaction_flags_TF_ONE_WAY,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };

        // Submit the transaction.
        driver
            .handle_transaction(&sender_task, &sender_proc, &sender_thread, transaction)
            .expect("failed to handle the transaction");

        // The thread is ineligible to take the command (not sleeping) so check the process queue.
        assert_matches!(
            receiver_proc.command_queue.lock().front(),
            Some(Command::OnewayTransaction(TransactionData { code: FIRST_TRANSACTION_CODE, .. }))
        );

        // The object should not have the transaction queued on it, as it was immediately scheduled.
        // But it should be marked as handling a oneway.
        assert!(
            object.lock().handling_oneway_transaction,
            "object oneway queue should be marked as being handled"
        );

        // Queue another transaction.
        const SECOND_TRANSACTION_CODE: u32 = 43;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: SECOND_TRANSACTION_CODE,
                flags: transaction_flags_TF_ONE_WAY,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };
        driver
            .handle_transaction(&sender_task, &sender_proc, &sender_thread, transaction)
            .expect("transaction queued");

        // There should now be an entry in the queue.
        assert_eq!(object.lock().oneway_transactions.len(), 1);

        // The process queue should be unchanged. Simulate dispatching the command.
        let buffer_addr = match receiver_proc
            .command_queue
            .lock()
            .pop_front()
            .expect("the first oneway transaction should be queued on the process")
        {
            Command::OnewayTransaction(TransactionData {
                code: FIRST_TRANSACTION_CODE,
                data_buffer,
                ..
            }) => data_buffer.address,
            _ => panic!("unexpected command in process queue"),
        };

        // Now the receiver issues the `BC_FREE_BUFFER` command, which should queue up the next
        // oneway transaction, guaranteeing sequential execution.
        driver.handle_free_buffer(&receiver_proc, buffer_addr).expect("failed to free buffer");

        assert!(object.lock().oneway_transactions.is_empty(), "oneway queue should now be empty");
        assert!(
            object.lock().handling_oneway_transaction,
            "object oneway queue should still be marked as being handled"
        );

        // The process queue should have a new transaction. Simulate dispatching the command.
        let buffer_addr = match receiver_proc
            .command_queue
            .lock()
            .pop_front()
            .expect("the second oneway transaction should be queued on the process")
        {
            Command::OnewayTransaction(TransactionData {
                code: SECOND_TRANSACTION_CODE,
                data_buffer,
                ..
            }) => data_buffer.address,
            _ => panic!("unexpected command in process queue"),
        };

        // Now the receiver issues the `BC_FREE_BUFFER` command, which should end oneway handling.
        driver.handle_free_buffer(&receiver_proc, buffer_addr).expect("failed to free buffer");

        assert!(object.lock().oneway_transactions.is_empty(), "oneway queue should still be empty");
        assert!(
            !object.lock().handling_oneway_transaction,
            "object oneway queue should no longer be marked as being handled"
        );
    }

    #[fuchsia::test]
    fn synchronous_transactions_bypass_oneway_transaction_queue() {
        let (kernel, sender_task) = create_kernel_and_task();
        let receiver_task = create_task(&kernel, "test-task2");

        let driver = BinderDriver::new();
        let (sender_proc, sender_thread) = driver.create_process_and_thread(sender_task.id);
        let (receiver_proc, _receiver_thread) = driver.create_process_and_thread(receiver_task.id);

        // Initialize the receiver process with shared memory in the driver.
        mmap_shared_memory(&driver, &receiver_task, &receiver_proc);

        // Insert a binder object for the receiver, and grab a handle to it in the sender.
        const OBJECT_ADDR: UserAddress = UserAddress::from(0x01);
        let object = register_binder_object(&receiver_proc, OBJECT_ADDR, OBJECT_ADDR + 1u64);
        let handle = sender_proc.handles.lock().insert_for_transaction(object.clone());

        // Construct a oneway transaction to send from the sender to the receiver.
        const ONEWAY_TRANSACTION_CODE: u32 = 42;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: ONEWAY_TRANSACTION_CODE,
                flags: transaction_flags_TF_ONE_WAY,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };

        // Submit the transaction twice so that the queue is populated.
        driver
            .handle_transaction(&sender_task, &sender_proc, &sender_thread, transaction)
            .expect("failed to handle the transaction");
        driver
            .handle_transaction(&sender_task, &sender_proc, &sender_thread, transaction)
            .expect("failed to handle the transaction");

        // The thread is ineligible to take the command (not sleeping) so check (and dequeue)
        // the process queue.
        assert_matches!(
            receiver_proc.command_queue.lock().pop_front(),
            Some(Command::OnewayTransaction(TransactionData { code: ONEWAY_TRANSACTION_CODE, .. }))
        );

        // The object should also have the second transaction queued on it.
        assert!(
            object.lock().handling_oneway_transaction,
            "object oneway queue should be marked as being handled"
        );
        assert_eq!(
            object.lock().oneway_transactions.len(),
            1,
            "object oneway queue should have second transaction queued"
        );

        // Queue a synchronous (request/response) transaction.
        const SYNC_TRANSACTION_CODE: u32 = 43;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: SYNC_TRANSACTION_CODE,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };
        driver
            .handle_transaction(&sender_task, &sender_proc, &sender_thread, transaction)
            .expect("sync transaction queued");

        assert_eq!(
            object.lock().oneway_transactions.len(),
            1,
            "oneway queue should not have grown"
        );

        // The process queue should now have the synchronous transaction queued.
        assert_matches!(
            receiver_proc.command_queue.lock().pop_front(),
            Some(Command::Transaction {
                data: TransactionData { code: SYNC_TRANSACTION_CODE, .. },
                ..
            })
        );
    }

    #[fuchsia::test]
    fn dead_reply_when_transaction_recipient_proc_dies() {
        let test = TranslateHandlesTestFixture::new();

        // Insert a binder object for the receiver, and grab a handle to it in the sender.
        const OBJECT_ADDR: UserAddress = UserAddress::from(0x01);
        let object = register_binder_object(&test.receiver_proc, OBJECT_ADDR, OBJECT_ADDR + 1u64);
        let handle = test.sender_proc.handles.lock().insert_for_transaction(object);

        // Construct a synchronous transaction to send from the sender to the receiver.
        const FIRST_TRANSACTION_CODE: u32 = 42;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: FIRST_TRANSACTION_CODE,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };

        // Submit the transaction.
        test.driver
            .handle_transaction(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                transaction,
            )
            .expect("failed to handle the transaction");

        // Check that there are no commands waiting for the sending thread.
        assert!(test.sender_thread.read().command_queue.is_empty());

        // Check that the receiving process has a transaction scheduled.
        assert_matches!(
            test.receiver_proc.command_queue.lock().front(),
            Some(Command::Transaction { .. })
        );

        // Drop the receiving process.
        let TranslateHandlesTestFixture { sender_thread, receiver_proc, .. } = test;
        test.driver.procs.write().remove(&receiver_proc.pid);
        drop(receiver_proc);

        // Check that there is a dead reply command for the sending thread.
        assert_matches!(sender_thread.read().command_queue.front(), Some(Command::DeadReply));
    }

    #[fuchsia::test]
    fn dead_reply_when_transaction_recipient_thread_dies() {
        let test = TranslateHandlesTestFixture::new();

        // Insert a binder object for the receiver, and grab a handle to it in the sender.
        const OBJECT_ADDR: UserAddress = UserAddress::from(0x01);
        let object = register_binder_object(&test.receiver_proc, OBJECT_ADDR, OBJECT_ADDR + 1u64);
        let handle = test.sender_proc.handles.lock().insert_for_transaction(object);

        // Construct a synchronous transaction to send from the sender to the receiver.
        const FIRST_TRANSACTION_CODE: u32 = 42;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: FIRST_TRANSACTION_CODE,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };

        // Create a thread for the receiver, and make it look eligible for transactions.
        // Pretend the client thread is waiting for commands, so that it can be scheduled commands.
        let receiver_thread = test
            .receiver_proc
            .thread_pool
            .write()
            .find_or_register_thread(&test.receiver_proc, test.receiver_proc.pid);
        let fake_waiter = Waiter::new();
        {
            let mut thread_state = receiver_thread.write();
            thread_state.registration = RegistrationState::MAIN;
            thread_state.waiter = fake_waiter.weak();
        }

        // Submit the transaction.
        test.driver
            .handle_transaction(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                transaction,
            )
            .expect("failed to handle the transaction");

        // Check that there are no commands waiting for the sending thread.
        assert!(test.sender_thread.read().command_queue.is_empty());

        // Check that the receiving thread has a transaction scheduled.
        assert_matches!(
            receiver_thread.read().command_queue.front(),
            Some(Command::Transaction { .. })
        );

        // Drop the receiving process and thread.
        let TranslateHandlesTestFixture { sender_thread, receiver_proc, .. } = test;
        test.driver.procs.write().remove(&receiver_proc.pid);
        drop(receiver_thread);
        drop(receiver_proc);

        // Check that there is a dead reply command for the sending thread.
        assert_matches!(sender_thread.read().command_queue.front(), Some(Command::DeadReply));
    }

    #[fuchsia::test]
    fn dead_reply_when_transaction_recipient_thread_dies_while_processing_reply() {
        let test = TranslateHandlesTestFixture::new();

        // Insert a binder object for the receiver, and grab a handle to it in the sender.
        const OBJECT_ADDR: UserAddress = UserAddress::from(0x01);
        let object = register_binder_object(&test.receiver_proc, OBJECT_ADDR, OBJECT_ADDR + 1u64);
        let handle = test.sender_proc.handles.lock().insert_for_transaction(object);

        // Construct a synchronous transaction to send from the sender to the receiver.
        const FIRST_TRANSACTION_CODE: u32 = 42;
        let transaction = binder_transaction_data_sg {
            transaction_data: binder_transaction_data {
                code: FIRST_TRANSACTION_CODE,
                target: binder_transaction_data__bindgen_ty_1 { handle: handle.into() },
                ..binder_transaction_data::default()
            },
            buffers_size: 0,
        };

        // Create a thread for the receiver, and make it look eligible for transactions.
        // Pretend the client thread is waiting for commands, so that it can be scheduled commands.
        let receiver_thread = test
            .receiver_proc
            .thread_pool
            .write()
            .find_or_register_thread(&test.receiver_proc, test.receiver_proc.pid);
        let fake_waiter = Waiter::new();
        {
            let mut thread_state = receiver_thread.write();
            thread_state.registration = RegistrationState::MAIN;
            thread_state.waiter = fake_waiter.weak();
        }

        // Submit the transaction.
        test.driver
            .handle_transaction(
                &test.sender_task,
                &test.sender_proc,
                &test.sender_thread,
                transaction,
            )
            .expect("failed to handle the transaction");

        // Check that there are no commands waiting for the sending thread.
        assert!(test.sender_thread.read().command_queue.is_empty());

        // Check that the receiving thread has a transaction scheduled.
        assert_matches!(
            receiver_thread.read().command_queue.front(),
            Some(Command::Transaction { .. })
        );

        // Have the thread dequeue the command.
        let read_buffer_addr = map_memory(&test.receiver_task, UserAddress::default(), *PAGE_SIZE);
        test.driver
            .handle_thread_read(
                &test.receiver_task,
                &test.receiver_proc,
                &receiver_thread,
                &UserBuffer { address: read_buffer_addr, length: *PAGE_SIZE as usize },
            )
            .expect("read command");

        // The thread should now have an empty command list and an ongoing transaction.
        assert!(receiver_thread.read().command_queue.is_empty());
        assert!(!receiver_thread.read().transactions.is_empty());

        // Drop the receiving process and thread.
        let TranslateHandlesTestFixture { sender_thread, receiver_proc, .. } = test;
        test.driver.procs.write().remove(&receiver_proc.pid);
        drop(receiver_thread);
        drop(receiver_proc);

        // Check that there is a dead reply command for the sending thread.
        assert_matches!(sender_thread.read().command_queue.front(), Some(Command::DeadReply));
    }

    // Simulates an mmap call on the binder driver, setting up shared memory between the driver and
    // `proc`.
    fn mmap_shared_memory(driver: &BinderDriver, task: &CurrentTask, proc: &Arc<BinderProcess>) {
        driver
            .mmap(
                task,
                proc,
                DesiredAddress::Hint(UserAddress::default()),
                VMO_LENGTH,
                zx::VmarFlags::PERM_READ,
                MappingOptions::empty(),
                NamespaceNode::new_anonymous(DirEntry::new_unrooted(Arc::new(FsNode::new_root(
                    PanickingFsNode,
                )))),
            )
            .expect("mmap");
    }

    /// Registers a binder object to `owner`.
    fn register_binder_object(
        owner: &Arc<BinderProcess>,
        weak_ref_addr: UserAddress,
        strong_ref_addr: UserAddress,
    ) -> Arc<BinderObject> {
        let object = Arc::new(BinderObject::new(
            owner,
            LocalBinderObject { weak_ref_addr, strong_ref_addr },
        ));
        owner.objects.lock().insert(weak_ref_addr, Arc::downgrade(&object));
        object
    }
}
