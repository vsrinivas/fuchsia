// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::collections::hash_map::Entry;
use std::collections::{HashMap, VecDeque};
use std::sync::{Arc, Weak};
use zerocopy::{AsBytes, FromBytes};

use crate::fs::*;
use crate::lock::RwLock;
use crate::task::*;
use crate::types::*;

/// Maximum depth of epoll instances monitoring one another.
/// From https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
const MAX_NESTED_DEPTH: u32 = 5;

/// WaitObject represents a FileHandle that is being waited upon.
/// The `data` field is a user defined quantity passed in
/// via `sys_epoll_ctl`. Typically C programs could use this
/// to store a pointer to the data that needs to be processed
/// after an event.
struct WaitObject {
    target: Weak<FileObject>,
    events: FdEvents,
    data: u64,
    cancel_key: WaitKey,
}

impl WaitObject {
    fn target(&self) -> Result<FileHandle, Errno> {
        self.target.upgrade().ok_or_else(|| errno!(EBADF))
    }
}

/// EpollKey acts as an key to a map of WaitObject.
/// In reality it is a pointer to a FileHandle object.
type EpollKey = usize;

fn as_epoll_key(file: &FileHandle) -> EpollKey {
    Arc::as_ptr(file) as EpollKey
}

/// ReadyObject represents an event on a waited upon object.
#[derive(Clone)]
struct ReadyObject {
    key: EpollKey,
    observed: FdEvents,
}

/// EpollEvent is a struct that is binary-identical
/// to `epoll_event` used in `epoll_ctl` and `epoll_pwait`.
#[derive(Default, Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(packed)]
pub struct EpollEvent {
    pub events: u32,
    pub data: u64,
}

/// EpollFileObject represents the FileObject used to
/// implement epoll_create1/epoll_ctl/epoll_pwait.
pub struct EpollFileObject {
    waiter: Waiter,
    /// Mutable state of this epoll object.
    state: Arc<RwLock<EpollState>>,
}

struct EpollState {
    /// Any file tracked by this epoll instance
    /// will exist as a key in `wait_objects`.
    wait_objects: HashMap<EpollKey, WaitObject>,
    /// trigger_list is a FIFO of events that have
    /// happened, but have not yet been processed.
    trigger_list: VecDeque<ReadyObject>,
    /// rearm_list is the list of event that need to
    /// be waited upon prior to actually waiting in
    /// EpollFileObject::wait. They cannot be re-armed
    /// before that, because, if the client process has
    /// not cleared the wait condition, they would just
    /// be immediately triggered.
    rearm_list: Vec<ReadyObject>,
    /// A list of waiters waiting for events from this
    /// epoll instance.
    waiters: WaitQueue,
}

impl EpollFileObject {
    /// Allocate a new, empty epoll object.
    pub fn new_file(current_task: &CurrentTask) -> FileHandle {
        Anon::new_file(
            current_task,
            Box::new(EpollFileObject {
                waiter: Waiter::new(),
                state: Arc::new(RwLock::new(EpollState {
                    wait_objects: HashMap::default(),
                    trigger_list: VecDeque::new(),
                    rearm_list: Vec::new(),
                    waiters: WaitQueue::default(),
                })),
            }),
            OpenFlags::RDWR,
        )
    }

    fn wait_on_file(
        &self,
        current_task: &CurrentTask,
        key: EpollKey,
        wait_object: &mut WaitObject,
    ) -> Result<(), Errno> {
        self.wait_on_file_with_options(current_task, key, wait_object, WaitAsyncOptions::empty())
    }

    fn wait_on_file_with_options(
        &self,
        current_task: &CurrentTask,
        key: EpollKey,
        wait_object: &mut WaitObject,
        options: WaitAsyncOptions,
    ) -> Result<(), Errno> {
        let state = self.state.clone();
        let handler = move |observed: FdEvents| {
            state.write().trigger_list.push_back(ReadyObject { key, observed })
        };

        wait_object.cancel_key = wait_object.target()?.wait_async(
            current_task,
            &self.waiter,
            wait_object.events,
            Box::new(handler),
            options,
        );
        Ok(())
    }

    /// Checks if this EpollFileObject monitors the `epoll_file_object` at `epoll_file_handle`.
    fn monitors(
        &self,
        epoll_file_handle: &FileHandle,
        epoll_file_object: &EpollFileObject,
        depth_left: u32,
    ) -> Result<bool, Errno> {
        if depth_left == 0 {
            return Ok(true);
        }

        let state = self.state.read();
        for nested_object in state.wait_objects.values() {
            match nested_object.target()?.downcast_file::<EpollFileObject>() {
                None => continue,
                Some(target) => {
                    if target.monitors(epoll_file_handle, epoll_file_object, depth_left - 1)?
                        || Arc::ptr_eq(&nested_object.target()?, epoll_file_handle)
                    {
                        return Ok(true);
                    }
                }
            }
        }

        Ok(false)
    }

    /// Asynchronously wait on certain events happening on a FileHandle.
    pub fn add(
        &self,
        current_task: &CurrentTask,
        file: &FileHandle,
        epoll_file_handle: &FileHandle,
        mut epoll_event: EpollEvent,
    ) -> Result<(), Errno> {
        let mode = file.name.entry.node.info().mode;
        if mode.is_reg() || mode.is_dir() {
            return error!(EPERM);
        }

        epoll_event.events |= FdEvents::POLLHUP.mask();
        epoll_event.events |= FdEvents::POLLERR.mask();

        // Check if adding this file would cause a cycle at a max depth of 5.
        if let Some(epoll_to_add) = file.downcast_file::<EpollFileObject>() {
            if epoll_to_add.monitors(epoll_file_handle, self, MAX_NESTED_DEPTH)? {
                return error!(ELOOP);
            }
        }

        let mut state = self.state.write();
        let key = as_epoll_key(file);
        match state.wait_objects.entry(key) {
            Entry::Occupied(_) => error!(EEXIST),
            Entry::Vacant(entry) => {
                let wait_object = entry.insert(WaitObject {
                    target: Arc::downgrade(file),
                    events: FdEvents::from(epoll_event.events),
                    data: epoll_event.data,
                    cancel_key: WaitKey::empty(),
                });
                self.wait_on_file(current_task, key, wait_object)
            }
        }
    }

    /// Modify the events we are looking for on a Filehandle.
    pub fn modify(
        &self,
        current_task: &CurrentTask,
        file: &FileHandle,
        mut epoll_event: EpollEvent,
    ) -> Result<(), Errno> {
        epoll_event.events |= FdEvents::POLLHUP.mask();
        epoll_event.events |= FdEvents::POLLERR.mask();

        let mut state = self.state.write();
        let key = as_epoll_key(file);
        state.rearm_list.retain(|x| x.key != key);
        match state.wait_objects.entry(key) {
            Entry::Occupied(mut entry) => {
                let wait_object = entry.get_mut();
                wait_object.target()?.cancel_wait(
                    current_task,
                    &self.waiter,
                    wait_object.cancel_key,
                );
                wait_object.events = FdEvents::from(epoll_event.events);
                self.wait_on_file(current_task, key, wait_object)
            }
            Entry::Vacant(_) => error!(ENOENT),
        }
    }

    /// Cancel an asynchronous wait on an object. Events triggered before
    /// calling this will still be delivered.
    pub fn delete(&self, current_task: &CurrentTask, file: &FileHandle) -> Result<(), Errno> {
        let mut state = self.state.write();
        let key = as_epoll_key(file);
        if let Some(wait_object) = state.wait_objects.remove(&key) {
            wait_object.target()?.cancel_wait(current_task, &self.waiter, wait_object.cancel_key);
            state.rearm_list.retain(|x| x.key != key);
            Ok(())
        } else {
            error!(ENOENT)
        }
    }

    /// Stores events from the trigger list in `pending_list`. This allows multiple
    /// threads to round robin through the available events in `trigger_list`.
    ///
    /// If an event in the trigger list is stale, waits on the file again.
    fn process_triggered_events(
        &self,
        current_task: &CurrentTask,
        pending_list: &mut Vec<ReadyObject>,
        max_events: i32,
    ) -> Result<(), Errno> {
        let mut state = self.state.write();
        while pending_list.len() < max_events as usize && !state.trigger_list.is_empty() {
            if let Some(pending) = state.trigger_list.pop_front() {
                if let Some(wait) = state.wait_objects.get_mut(&pending.key) {
                    let observed = wait.target()?.query_events(current_task);
                    if observed & wait.events {
                        let ready = ReadyObject { key: pending.key, observed };
                        pending_list.push(ready);
                    } else {
                        self.wait_on_file(current_task, pending.key, wait)?;
                    }
                }
            }
        }
        Ok(())
    }

    /// Waits until an event exists in `pending_list` or until `timeout` has
    /// been reached.
    fn wait_until_pending_event(
        &self,
        current_task: &CurrentTask,
        pending_list: &mut Vec<ReadyObject>,
        max_events: i32,
        timeout: zx::Duration,
    ) -> Result<(), Errno> {
        if !pending_list.is_empty() {
            return Ok(());
        }

        let mut wait_deadline = zx::Time::after(timeout);

        // The handlers in the waits cause items to be appended
        // to trigger_list. See the closure in `wait_on_file` to see
        // how this happens.
        loop {
            match self.waiter.wait_until(current_task, wait_deadline) {
                Err(err) if err == ETIMEDOUT => break,
                result => result?,
            }

            // For each successful wait, we take an item off
            // the trigger list and store it locally. This allows
            // multiple threads to call EpollFileObject::wait
            // simultaneously.  We break early if the max_events
            // is reached, leaving items on the trigger list for
            // the next wait.
            self.process_triggered_events(current_task, pending_list, max_events)?;
            if pending_list.len() == max_events as usize {
                break;
            }
            wait_deadline = zx::Time::ZERO;
        }

        Ok(())
    }

    /// Blocking wait on all waited upon events with a timeout.
    pub fn wait(
        &self,
        current_task: &CurrentTask,
        max_events: i32,
        timeout: zx::Duration,
    ) -> Result<Vec<EpollEvent>, Errno> {
        // First we start waiting again on wait objects that have
        // previously been triggered.
        {
            let mut state = self.state.write();
            let rearm_list = std::mem::take(&mut state.rearm_list);
            for to_wait in rearm_list.iter() {
                // TODO handle interrupts here
                let w = state.wait_objects.get_mut(&to_wait.key).unwrap();
                self.wait_on_file(current_task, to_wait.key, w)?;
            }
        }

        // Process any events that are already available.
        let mut pending_list: Vec<ReadyObject> = vec![];
        self.process_triggered_events(current_task, &mut pending_list, max_events)?;

        self.wait_until_pending_event(current_task, &mut pending_list, max_events, timeout)?;

        // Process the pending list and add processed ReadyObject
        // entries to the rearm_list for the next wait.
        let mut result = vec![];
        let mut state = self.state.write();
        for pending_event in pending_list.iter() {
            // The wait could have been deleted by here,
            // so ignore the None case.
            if let Some(wait) = state.wait_objects.get_mut(&pending_event.key) {
                let reported_events = pending_event.observed.mask() & wait.events.mask();
                result.push(EpollEvent { events: reported_events, data: wait.data });

                // Files marked with `EPOLLONESHOT` should only notify
                // once and need to be rearmed manually with epoll_ctl_mod().
                if wait.events.mask() & EPOLLONESHOT != 0 {
                    continue;
                }
                if wait.events.mask() & EPOLLET != 0 {
                    self.wait_on_file_with_options(
                        current_task,
                        pending_event.key,
                        wait,
                        WaitAsyncOptions::EDGE_TRIGGERED,
                    )?;
                } else {
                    state.rearm_list.push(pending_event.clone());
                }
            }
        }

        // Notify waiters of unprocessed events.
        if !state.trigger_list.is_empty() {
            state.waiters.notify_events(FdEvents::POLLIN);
        }

        Ok(result)
    }
}

impl FileOps for EpollFileObject {
    fileops_impl_nonseekable!();

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
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
        let present_events = self.query_events(current_task);
        if events & present_events && !options.contains(WaitAsyncOptions::EDGE_TRIGGERED) {
            waiter.wake_immediately(present_events.mask(), handler)
        } else {
            let mut state = self.state.write();
            state.waiters.wait_async_mask(waiter, events.mask(), handler)
        }
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, _waiter: &Waiter, key: WaitKey) {
        let mut state = self.state.write();
        state.waiters.cancel_wait(key);
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        let mut events = FdEvents::empty();
        if self.state.read().trigger_list.is_empty() {
            events |= FdEvents::POLLIN;
        }
        events
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::fuchsia::create_fuchsia_pipe;
    use crate::fs::pipe::new_pipe;
    use crate::fs::socket::{SocketDomain, SocketType, UnixSocket};
    use crate::fs::FdEvents;
    use crate::mm::MemoryAccessor;
    use crate::mm::PAGE_SIZE;
    use crate::types::UserBuffer;
    use fuchsia_zircon::HandleBased;
    use std::sync::atomic::{AtomicU64, Ordering};
    use syncio::Zxio;

    use crate::testing::*;

    #[::fuchsia::test]
    fn test_epoll_read_ready() {
        static WRITE_COUNT: AtomicU64 = AtomicU64::new(0);
        const EVENT_DATA: u64 = 42;

        let (kernel, _init_task) = create_kernel_and_task();
        let current_task = create_task(&kernel, "main-task");
        let writer_task = create_task(&kernel, "writer-task");

        let (pipe_out, pipe_in) = new_pipe(&current_task).unwrap();

        let test_string = "hello starnix".to_string();
        let test_bytes = test_string.as_bytes();
        let test_len = test_bytes.len();
        let read_mem = map_memory(&current_task, UserAddress::default(), test_len as u64);
        let read_buf = [UserBuffer { address: read_mem, length: test_len }];

        let write_mem = map_memory(&writer_task, UserAddress::default(), test_len as u64);
        let write_buf = [UserBuffer { address: write_mem, length: test_len }];
        writer_task.mm.write_memory(write_mem, test_bytes).unwrap();

        let epoll_file_handle = EpollFileObject::new_file(&current_task);
        let epoll_file = epoll_file_handle.downcast_file::<EpollFileObject>().unwrap();
        epoll_file
            .add(
                &current_task,
                &pipe_out,
                &epoll_file_handle,
                EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
            )
            .unwrap();

        let thread = std::thread::spawn(move || {
            let bytes_written = pipe_in.write(&writer_task, &write_buf).unwrap();
            assert_eq!(bytes_written, test_len);
            WRITE_COUNT.fetch_add(bytes_written as u64, Ordering::Relaxed);
        });
        let events = epoll_file.wait(&current_task, 10, zx::Duration::INFINITE).unwrap();
        let _ = thread.join();
        assert_eq!(1, events.len());
        let event = &events[0];
        assert!(FdEvents::from(event.events) & FdEvents::POLLIN);
        let data = event.data;
        assert_eq!(EVENT_DATA, data);

        let bytes_read = pipe_out.read(&current_task, &read_buf).unwrap();
        assert_eq!(bytes_read as u64, WRITE_COUNT.load(Ordering::Relaxed));
        assert_eq!(bytes_read, test_len);
        let mut read_data = vec![0u8; test_len];
        current_task.mm.read_memory(read_buf[0].address, &mut read_data).unwrap();
        assert_eq!(read_data.as_bytes(), test_bytes);
    }

    #[::fuchsia::test]
    fn test_epoll_ready_then_wait() {
        const EVENT_DATA: u64 = 42;

        let (_kernel, current_task) = create_kernel_and_task();

        let (pipe_out, pipe_in) = new_pipe(&current_task).unwrap();

        let test_string = "hello starnix".to_string();
        let test_bytes = test_string.as_bytes();
        let test_len = test_bytes.len();
        let read_mem = map_memory(&current_task, UserAddress::default(), test_len as u64);
        let read_buf = [UserBuffer { address: read_mem, length: test_len }];
        let write_mem = map_memory(&current_task, UserAddress::default(), test_len as u64);
        let write_buf = [UserBuffer { address: write_mem, length: test_len }];

        current_task.mm.write_memory(write_mem, test_bytes).unwrap();

        assert_eq!(pipe_in.write(&current_task, &write_buf).unwrap(), test_bytes.len());

        let epoll_file_handle = EpollFileObject::new_file(&current_task);
        let epoll_file = epoll_file_handle.downcast_file::<EpollFileObject>().unwrap();
        epoll_file
            .add(
                &current_task,
                &pipe_out,
                &epoll_file_handle,
                EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
            )
            .unwrap();

        let events = epoll_file.wait(&current_task, 10, zx::Duration::INFINITE).unwrap();
        assert_eq!(1, events.len());
        let event = &events[0];
        assert!(FdEvents::from(event.events) & FdEvents::POLLIN);
        let data = event.data;
        assert_eq!(EVENT_DATA, data);

        let bytes_read = pipe_out.read(&current_task, &read_buf).unwrap();
        assert_eq!(bytes_read, test_len);
        let mut read_data = vec![0u8; test_len];
        current_task.mm.read_memory(read_buf[0].address, &mut read_data).unwrap();
        assert_eq!(read_data.as_bytes(), test_bytes);
    }

    #[::fuchsia::test]
    fn test_epoll_ctl_cancel() {
        for do_cancel in [true, false] {
            let (_kernel, current_task) = create_kernel_and_task();
            let event = new_eventfd(&current_task, 0, EventFdType::Counter, true);
            let waiter = Waiter::new();

            let epoll_file_handle = EpollFileObject::new_file(&current_task);
            let epoll_file = epoll_file_handle.downcast_file::<EpollFileObject>().unwrap();
            const EVENT_DATA: u64 = 42;
            epoll_file
                .add(
                    &current_task,
                    &event,
                    &epoll_file_handle,
                    EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
                )
                .unwrap();

            if do_cancel {
                epoll_file.delete(&current_task, &event).unwrap();
            }

            let callback_count = Arc::new(AtomicU64::new(0));
            let callback_count_clone = callback_count.clone();
            let handler = move |_observed: FdEvents| {
                callback_count_clone.fetch_add(1, Ordering::Relaxed);
            };
            let key = event.wait_async(
                &current_task,
                &waiter,
                FdEvents::POLLIN,
                Box::new(handler),
                WaitAsyncOptions::empty(),
            );
            if do_cancel {
                event.cancel_wait(&current_task, &waiter, key);
            }

            let write_mem = map_memory(
                &current_task,
                UserAddress::default(),
                std::mem::size_of::<u64>() as u64,
            );
            let add_val = 1u64;
            current_task.mm.write_memory(write_mem, &add_val.to_ne_bytes()).unwrap();
            let data = [UserBuffer { address: write_mem, length: std::mem::size_of::<u64>() }];
            assert_eq!(event.write(&current_task, &data).unwrap(), std::mem::size_of::<u64>());

            let events = epoll_file.wait(&current_task, 10, zx::Duration::from_seconds(0)).unwrap();

            if do_cancel {
                assert_eq!(0, events.len());
            } else {
                assert_eq!(1, events.len());
                let event = &events[0];
                assert!(FdEvents::from(event.events) & FdEvents::POLLIN);
                let data = event.data;
                assert_eq!(EVENT_DATA, data);
            }
        }
    }

    #[::fuchsia::test]
    fn test_multiple_events() {
        let (_kernel, current_task) = create_kernel_and_task();
        let address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        let (client1, server1) =
            zx::Socket::create(zx::SocketOpts::empty()).expect("Socket::create");
        let (client2, server2) =
            zx::Socket::create(zx::SocketOpts::empty()).expect("Socket::create");
        let pipe1 = create_fuchsia_pipe(&current_task, client1, OpenFlags::RDWR)
            .expect("create_fuchsia_pipe");
        let pipe2 = create_fuchsia_pipe(&current_task, client2, OpenFlags::RDWR)
            .expect("create_fuchsia_pipe");
        let server1_zxio = Zxio::create(server1.into_handle()).expect("Zxio::create");
        let server2_zxio = Zxio::create(server2.into_handle()).expect("Zxio::create");

        let poll = || {
            let epoll_object = EpollFileObject::new_file(&current_task);
            let epoll_file = epoll_object.downcast_file::<EpollFileObject>().unwrap();
            epoll_file
                .add(
                    &current_task,
                    &pipe1,
                    &epoll_object,
                    EpollEvent { events: FdEvents::POLLIN.mask(), data: 1 },
                )
                .expect("epoll_file.add");
            epoll_file
                .add(
                    &current_task,
                    &pipe2,
                    &epoll_object,
                    EpollEvent { events: FdEvents::POLLIN.mask(), data: 2 },
                )
                .expect("epoll_file.add");
            epoll_file.wait(&current_task, 2, zx::Duration::from_millis(0)).expect("wait")
        };

        let fds = poll();
        assert!(fds.is_empty());

        assert_eq!(server1_zxio.write(&[0]).expect("write"), 1);

        let fds = poll();
        assert_eq!(fds.len(), 1);
        assert_eq!(FdEvents::from(fds[0].events), FdEvents::POLLIN);
        let data = fds[0].data;
        assert_eq!(data, 1);
        assert_eq!(
            pipe1.read(&current_task, &[UserBuffer { address, length: 64 }]).expect("read"),
            1
        );

        let fds = poll();
        assert!(fds.is_empty());

        assert_eq!(server2_zxio.write(&[0]).expect("write"), 1);

        let fds = poll();
        assert_eq!(fds.len(), 1);
        assert_eq!(FdEvents::from(fds[0].events), FdEvents::POLLIN);
        let data = fds[0].data;
        assert_eq!(data, 2);
        assert_eq!(
            pipe2.read(&current_task, &[UserBuffer { address, length: 64 }]).expect("read"),
            1
        );

        let fds = poll();
        assert!(fds.is_empty());
    }

    #[::fuchsia::test]
    fn test_cancel_after_notify() {
        let (_kernel, current_task) = create_kernel_and_task();
        let event = new_eventfd(&current_task, 0, EventFdType::Counter, true);
        let epoll_file_handle = EpollFileObject::new_file(&current_task);
        let epoll_file = epoll_file_handle.downcast_file::<EpollFileObject>().unwrap();

        // Add a thing
        const EVENT_DATA: u64 = 42;
        epoll_file
            .add(
                &current_task,
                &event,
                &epoll_file_handle,
                EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
            )
            .unwrap();

        // Make the thing send a notification, wait for it
        let write_mem =
            map_memory(&current_task, UserAddress::default(), std::mem::size_of::<u64>() as u64);
        let add_val = 1u64;
        current_task.mm.write_memory(write_mem, &add_val.to_ne_bytes()).unwrap();
        let data = [UserBuffer { address: write_mem, length: std::mem::size_of::<u64>() }];
        assert_eq!(event.write(&current_task, &data).unwrap(), std::mem::size_of::<u64>());

        assert_eq!(
            epoll_file.wait(&current_task, 10, zx::Duration::from_seconds(0)).unwrap().len(),
            1
        );

        // Remove the thing
        epoll_file.delete(&current_task, &event).unwrap();

        // Wait for new notifications
        assert_eq!(
            epoll_file.wait(&current_task, 10, zx::Duration::from_seconds(0)).unwrap().len(),
            0
        );
        // That shouldn't crash
    }

    #[::fuchsia::test]
    fn test_add_then_modify() {
        let (_kernel, current_task) = create_kernel_and_task();
        let (socket1, _socket2) = UnixSocket::new_pair(
            &current_task,
            SocketDomain::Unix,
            SocketType::Stream,
            OpenFlags::RDWR,
        )
        .expect("Failed to create socket pair.");

        let epoll_file_handle = EpollFileObject::new_file(&current_task);
        let epoll_file = epoll_file_handle.downcast_file::<EpollFileObject>().unwrap();

        const EVENT_DATA: u64 = 42;
        epoll_file
            .add(
                &current_task,
                &socket1,
                &epoll_file_handle,
                EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
            )
            .unwrap();
        assert_eq!(
            epoll_file.wait(&current_task, 10, zx::Duration::from_seconds(0)).unwrap().len(),
            0
        );

        let read_write_event = FdEvents::POLLIN | FdEvents::POLLOUT;
        epoll_file
            .modify(
                &current_task,
                &socket1,
                EpollEvent { events: read_write_event.mask(), data: EVENT_DATA },
            )
            .unwrap();
        let triggered_events =
            epoll_file.wait(&current_task, 10, zx::Duration::from_seconds(0)).unwrap();
        assert_eq!(1, triggered_events.len());
        let event = &triggered_events[0];
        let events = event.events;
        assert_eq!(events, FdEvents::POLLOUT.mask());
        let data = event.data;
        assert_eq!(EVENT_DATA, data);
    }
}
