// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::lock::RwLock;
use crate::task::*;
use crate::types::*;
use fuchsia_zircon as zx;
use std::collections::hash_map::Entry;
use std::collections::{HashMap, VecDeque};
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

/// WaitObject represnets a FileHandle that is being waited upon.
/// The `data` field is a user defined quantity passed in
/// via `sys_epoll_ctl`. Typically C programs could use this
/// to store a pointer to the data that needs to be processed
/// after an event.
struct WaitObject {
    target: FileHandle,
    events: FdEvents,
    data: u64,
    cancel_key: WaitKey,
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
    waiter: Arc<Waiter>,
    /// Mutable state of this epoll object.
    state: Arc<RwLock<EpollState>>,
}

struct EpollState {
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
}

impl EpollFileObject {
    /// Allocate a new, empty epoll object.
    pub fn new(kernel: &Kernel) -> FileHandle {
        Anon::new_file(
            anon_fs(kernel),
            Box::new(EpollFileObject {
                waiter: Waiter::new(),
                state: Arc::new(RwLock::new(EpollState {
                    wait_objects: HashMap::default(),
                    trigger_list: VecDeque::new(),
                    rearm_list: Vec::new(),
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

        wait_object.cancel_key = wait_object.target.wait_async(
            current_task,
            &self.waiter,
            wait_object.events,
            Box::new(handler),
            options,
        );
        Ok(())
    }

    /// Asynchronously wait on certain events happening on a FileHandle.
    pub fn add(
        &self,
        current_task: &CurrentTask,
        file: &FileHandle,
        mut epoll_event: EpollEvent,
    ) -> Result<(), Errno> {
        epoll_event.events |= FdEvents::POLLHUP.mask();
        epoll_event.events |= FdEvents::POLLERR.mask();

        let mut state = self.state.write();
        let key = as_epoll_key(&file);
        match state.wait_objects.entry(key) {
            Entry::Occupied(_) => return error!(EEXIST),
            Entry::Vacant(entry) => {
                let wait_object = entry.insert(WaitObject {
                    target: file.clone(),
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
        let key = as_epoll_key(&file);
        state.rearm_list.retain(|x| x.key != key);
        match state.wait_objects.entry(key) {
            Entry::Occupied(mut entry) => {
                let wait_object = entry.get_mut();
                wait_object.target.cancel_wait(&current_task, &self.waiter, wait_object.cancel_key);
                wait_object.events = FdEvents::from(epoll_event.events);
                self.wait_on_file(current_task, key, wait_object)?;
                Ok(())
            }
            Entry::Vacant(_) => return error!(ENOENT),
        }
    }

    /// Cancel an asynchronous wait on an object. Events triggered before
    /// calling this will still be delivered.
    pub fn delete(&self, current_task: &CurrentTask, file: &FileHandle) -> Result<(), Errno> {
        let mut state = self.state.write();
        let key = as_epoll_key(&file);
        if let Some(wait_object) = state.wait_objects.remove(&key) {
            wait_object.target.cancel_wait(&current_task, &self.waiter, wait_object.cancel_key);
            state.rearm_list.retain(|x| x.key != key);
            Ok(())
        } else {
            error!(ENOENT)
        }
    }

    /// Blocking wait on all waited upon events with a timeout.
    pub fn wait(
        &self,
        current_task: &CurrentTask,
        max_events: i32,
        timeout: i32,
    ) -> Result<Vec<EpollEvent>, Errno> {
        // First we start waiting again on wait objects that have
        // previously been triggered.
        {
            let mut state = self.state.write();
            let rearm_list = std::mem::take(&mut state.rearm_list);
            for to_wait in rearm_list.clone().iter() {
                // TODO handle interrupts here
                let w = state.wait_objects.get_mut(&to_wait.key).unwrap();
                self.wait_on_file(current_task, to_wait.key, w)?;
            }
        }

        let mut wait_deadline = if timeout == -1 {
            zx::Time::INFINITE
        } else if timeout >= 0 {
            let millis = timeout as i64;
            zx::Time::after(zx::Duration::from_millis(millis))
        } else {
            return error!(EINVAL);
        };

        // The handlers in the waits cause items to be appended
        // to trigger_list. See the closure in `wait_on_file` to see
        // how this happens.
        let mut pending_list: Vec<ReadyObject> = vec![];
        loop {
            match self.waiter.wait_until(&current_task, wait_deadline) {
                Err(err) if err == ETIMEDOUT => break,
                result => result?,
            }

            // For each sucessful wait, we take an item off
            // the trigger list and store it locally. This allows
            // multiple threads to call EpollFileObject::wait
            // simultaneously.  We break early if the max_events
            // is reached, leaving items on the trigger list for
            // the next wait.
            let mut state = self.state.write();
            if let Some(pending) = state.trigger_list.pop_front() {
                if let Some(wait) = state.wait_objects.get_mut(&pending.key) {
                    let observed = wait.target.query_events(current_task);
                    if observed & wait.events {
                        let ready = ReadyObject { key: pending.key, observed };
                        pending_list.push(ready);
                        if pending_list.len() == max_events as usize {
                            break;
                        }
                        wait_deadline = zx::Time::ZERO;
                    } else {
                        self.wait_on_file(current_task, pending.key, wait)?;
                    }
                }
            }
        }

        // Process the pending list and add processed ReadyObject
        // enties to the rearm_list for the next wait.
        let mut result = vec![];
        let mut state = self.state.write();
        for pending_event in pending_list.iter() {
            // The wait could have been deleted by here,
            // so ignore the None case.
            if let Some(wait) = state.wait_objects.get_mut(&pending_event.key) {
                let reported_events = pending_event.observed.mask() & wait.events.mask();
                result.push(EpollEvent { events: reported_events, data: wait.data });

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

    // TODO implement blocking for epoll
    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _waiter: &Arc<Waiter>,
        _events: FdEvents,
        _handler: EventHandler,
        _options: WaitAsyncOptions,
    ) -> WaitKey {
        panic!("waiting on epoll unimplemnted")
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, _waiter: &Arc<Waiter>, _key: WaitKey) {
        panic!("cancelling waiting on epoll unimplemnted")
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        panic!("querying epoll unimplemnted")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::pipe::new_pipe;
    use crate::fs::FdEvents;
    use crate::types::UserBuffer;
    use std::sync::atomic::{AtomicU64, Ordering};

    use crate::testing::*;

    #[::fuchsia::test]
    fn test_epoll_read_ready() {
        static WRITE_COUNT: AtomicU64 = AtomicU64::new(0);
        const EVENT_DATA: u64 = 42;

        let (kernel, _init_task) = create_kernel_and_task();
        let current_task = create_task(&kernel, "main-task");
        let writer_task = create_task(&kernel, "writer-task");

        let (pipe_out, pipe_in) = new_pipe(&current_task).unwrap();

        let test_string = "hello startnix".to_string();
        let test_bytes = test_string.as_bytes();
        let test_len = test_bytes.len();
        let read_mem = map_memory(&current_task, UserAddress::default(), test_len as u64);
        let read_buf = [UserBuffer { address: read_mem, length: test_len }];

        let write_mem = map_memory(&writer_task, UserAddress::default(), test_len as u64);
        let write_buf = [UserBuffer { address: write_mem, length: test_len }];
        writer_task.mm.write_memory(write_mem, test_bytes).unwrap();

        let epoll_file = EpollFileObject::new(&kernel);
        let epoll_file = epoll_file.downcast_file::<EpollFileObject>().unwrap();
        epoll_file
            .add(
                &current_task,
                &pipe_out,
                EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
            )
            .unwrap();

        let thread = std::thread::spawn(move || {
            let bytes_written = pipe_in.write(&writer_task, &write_buf).unwrap();
            assert_eq!(bytes_written, test_len);
            WRITE_COUNT.fetch_add(bytes_written as u64, Ordering::Relaxed);
        });
        let events = epoll_file.wait(&current_task, 10, -1).unwrap();
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

        let (kernel, current_task) = create_kernel_and_task();

        let (pipe_out, pipe_in) = new_pipe(&current_task).unwrap();

        let test_string = "hello startnix".to_string();
        let test_bytes = test_string.as_bytes();
        let test_len = test_bytes.len();
        let read_mem = map_memory(&current_task, UserAddress::default(), test_len as u64);
        let read_buf = [UserBuffer { address: read_mem, length: test_len }];
        let write_mem = map_memory(&current_task, UserAddress::default(), test_len as u64);
        let write_buf = [UserBuffer { address: write_mem, length: test_len }];

        current_task.mm.write_memory(write_mem, test_bytes).unwrap();

        assert_eq!(pipe_in.write(&current_task, &write_buf).unwrap(), test_bytes.len());

        let epoll_file = EpollFileObject::new(&kernel);
        let epoll_file = epoll_file.downcast_file::<EpollFileObject>().unwrap();
        epoll_file
            .add(
                &current_task,
                &pipe_out,
                EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA },
            )
            .unwrap();

        let events = epoll_file.wait(&current_task, 10, -1).unwrap();
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
            let (kernel, current_task) = create_kernel_and_task();
            let event = new_eventfd(&kernel, 0, EventFdType::Counter, true);
            let waiter = Waiter::new();

            let epoll_file = EpollFileObject::new(&kernel);
            let epoll_file = epoll_file.downcast_file::<EpollFileObject>().unwrap();
            const EVENT_DATA: u64 = 42;
            epoll_file
                .add(
                    &current_task,
                    &event,
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

            let events = epoll_file.wait(&current_task, 10, 0).unwrap();

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
    fn test_cancel_after_notify() {
        let (kernel, current_task) = create_kernel_and_task();
        let event = new_eventfd(&kernel, 0, EventFdType::Counter, true);
        let epoll_file = EpollFileObject::new(&kernel);
        let epoll_file = epoll_file.downcast_file::<EpollFileObject>().unwrap();

        // Add a thing
        const EVENT_DATA: u64 = 42;
        epoll_file
            .add(
                &current_task,
                &event,
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

        assert_eq!(epoll_file.wait(&current_task, 10, 0).unwrap().len(), 1);

        // Remove the thing
        epoll_file.delete(&current_task, &event).unwrap();

        // Wait for new notifications
        assert_eq!(epoll_file.wait(&current_task, 10, 0).unwrap().len(), 0);
        // That shouldn't crash
    }
}
