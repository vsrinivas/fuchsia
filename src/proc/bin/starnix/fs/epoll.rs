// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::not_implemented;
use crate::task::*;
use crate::types::*;
use fuchsia_zircon as zx;
use parking_lot::{Mutex, RwLock};
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
#[derive(Default, AsBytes, FromBytes)]
#[repr(packed)]
pub struct EpollEvent {
    pub events: u32,
    pub data: u64,
}

/// EpollFileObject represents the FileObject used to
/// implement epoll_create1/epoll_ctl/epoll_pwait.
pub struct EpollFileObject {
    waiter: Arc<Waiter>,
    wait_objects: RwLock<HashMap<EpollKey, WaitObject>>,
    // trigger_list is a FIFO of events that have
    // happened, but have not yet been processed.
    trigger_list: Arc<Mutex<VecDeque<ReadyObject>>>,
    // rearm_list is the list of event that need to
    // be waited upon prior to actually waiting in
    // EpollFileObject::wait. They cannot be re-armed
    // before that, because, if the client process has
    // not cleared the wait condition, they would just
    // be immediately triggered.
    rearm_list: Arc<Mutex<Vec<ReadyObject>>>,
}

impl EpollFileObject {
    /// Allocate a new, empty epoll object.
    pub fn new(kernel: &Kernel) -> FileHandle {
        Anon::new_file(
            anon_fs(kernel),
            Box::new(EpollFileObject {
                waiter: Waiter::new(),
                wait_objects: RwLock::new(HashMap::default()),
                trigger_list: Arc::new(Mutex::new(VecDeque::new())),
                rearm_list: Arc::new(Mutex::new(Vec::new())),
            }),
            OpenFlags::RDWR,
        )
    }

    fn wait_on_file(&self, key: EpollKey, wait_object: &WaitObject) -> Result<(), Errno> {
        let trigger_list = self.trigger_list.clone();
        let handler =
            move |observed: FdEvents| trigger_list.lock().push_back(ReadyObject { key, observed });

        wait_object.target.wait_async(&self.waiter, wait_object.events, Box::new(handler));
        Ok(())
    }

    /// Asynchronously wait on certain events happening on a FileHandle.
    pub fn add(&self, file: &FileHandle, epoll_event: EpollEvent) -> Result<(), Errno> {
        let mut waits = self.wait_objects.write();
        let key = as_epoll_key(&file);
        match waits.entry(key) {
            Entry::Occupied(_) => return error!(EEXIST),
            Entry::Vacant(entry) => {
                let wait_object = entry.insert(WaitObject {
                    target: file.clone(),
                    events: FdEvents::from(epoll_event.events),
                    data: epoll_event.data,
                });
                self.wait_on_file(key, wait_object)
            }
        }
    }

    /// Modify the events we are looking for on a Filehandle.
    pub fn modify(&self, _file: &FileHandle, _epoll_event: EpollEvent) -> Result<(), Errno> {
        not_implemented!("cannot modify a wait_async");
        error!(ENOSYS)
    }

    /// Cancel an asynchronous wait on an object. Events triggered before
    /// calling this will still be delivered.
    pub fn delete(&self, _file: &FileHandle) -> Result<(), Errno> {
        not_implemented!("cannot cancel a wait_async");
        error!(ENOSYS)
    }

    /// Blocking wait on all waited upon events with a timeout.
    pub fn wait(
        &self,
        current: &Task,
        max_events: i32,
        timeout: i32,
    ) -> Result<Vec<EpollEvent>, Errno> {
        // First we start waiting again on wait objects that have
        // previously been triggered.
        {
            let mut rearm_list = self.rearm_list.lock();
            let waits = self.wait_objects.write();
            for to_wait in rearm_list.iter() {
                // TODO handle interrupts here
                let w = waits.get(&to_wait.key).ok_or(errno!(EFAULT))?;
                self.wait_on_file(to_wait.key, w)?;
            }
            rearm_list.clear();
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
            match self.waiter.wait_until(current, wait_deadline) {
                Ok(_) => {}
                Err(err) => {
                    if err == ETIMEDOUT {
                        break;
                    }
                    return error!(err);
                }
            }

            // For each sucessful wait, we take an item off
            // the trigger list and store it locally. This allows
            // multiple threads to call EpollFileObject::wait
            // simultaneously.  We break early if the max_events
            // is reached, leaving items on the trigger list for
            //the next wait.
            let mut trigger_list = self.trigger_list.lock();
            if let Some(pending) = trigger_list.pop_front() {
                pending_list.push(pending);
                if pending_list.len() == max_events as usize {
                    break;
                }
            }
            wait_deadline = zx::Time::ZERO;
        }

        // Process the pening list and add processed ReadyObject
        // enties to the rearm_list for the next wait.
        let mut result = vec![];
        let mut rearm_list = self.rearm_list.lock();
        let wait_objects = self.wait_objects.read();
        for pending_event in pending_list.iter() {
            // The wait could have been deleted by here,
            // so ignore the None case.
            if let Some(wait) = wait_objects.get(&pending_event.key) {
                result.push(EpollEvent { events: pending_event.observed.mask(), data: wait.data });
                // TODO When edge-triggered epoll, EPOLLET, is
                // implemented, we would enable to wait here,
                // instead of adding it to the rearm list.
                rearm_list.push(pending_event.clone());
            }
        }

        Ok(result)
    }
}

impl FileOps for EpollFileObject {
    fd_impl_nonseekable!();

    fn write(
        &self,
        _file: &FileObject,
        _task: &Task,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::pipe::new_pipe;
    use crate::fs::FdEvents;
    use crate::syscalls::SyscallContext;
    use crate::types::UserBuffer;
    use fuchsia_async as fasync;
    use std::sync::atomic::{AtomicU64, Ordering};

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_epoll_read_ready() {
        static WRITE_COUNT: AtomicU64 = AtomicU64::new(0);
        const EVENT_DATA: u64 = 42;

        let (kernel, task_owner) = create_kernel_and_task();

        let task = &task_owner.task;
        let ctx = SyscallContext::new(&task_owner.task);
        let (pipe_out, pipe_in) = new_pipe(&kernel).unwrap();

        let test_string = "hello startnix".to_string();
        let test_bytes = test_string.as_bytes();
        let test_len = test_bytes.len();
        let read_mem = map_memory(&ctx, UserAddress::default(), test_len as u64);
        let read_buf = [UserBuffer { address: read_mem, length: test_len }];
        let write_mem = map_memory(&ctx, UserAddress::default(), test_len as u64);
        let write_buf = [UserBuffer { address: write_mem, length: test_len }];

        task.mm.write_memory(write_mem, test_bytes).unwrap();

        let epoll_file = EpollFileObject::new(&kernel);
        let epoll_file = epoll_file.downcast_file::<EpollFileObject>().unwrap();
        epoll_file
            .add(&pipe_out, EpollEvent { events: FdEvents::POLLIN.mask(), data: EVENT_DATA })
            .unwrap();

        let task_clone = task.clone();
        let thread = std::thread::spawn(move || {
            let bytes_written = pipe_in.write(&task_clone, &write_buf).unwrap();
            assert_eq!(bytes_written, test_len);
            WRITE_COUNT.fetch_add(bytes_written as u64, Ordering::Relaxed);
        });
        let events = epoll_file.wait(&task, 10, -1).unwrap();
        let _ = thread.join();
        assert_eq!(1, events.len());
        let event = &events[0];
        assert!(FdEvents::from(event.events) & FdEvents::POLLIN);
        let data = event.data;
        assert_eq!(EVENT_DATA, data);

        let bytes_read = pipe_out.read(task, &read_buf).unwrap();
        assert_eq!(bytes_read as u64, WRITE_COUNT.load(Ordering::Relaxed));
        assert_eq!(bytes_read, test_len);
        let mut read_data = vec![0u8; test_len];
        task.mm.read_memory(read_buf[0].address, &mut read_data).unwrap();
        assert_eq!(read_data.as_bytes(), test_bytes);
    }
}
