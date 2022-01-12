// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::task::*;
use crate::types::*;
use parking_lot::Mutex;
use std::sync::Arc;

const DATA_SIZE: usize = 8;

pub enum EventFdType {
    Counter,
    Semaphore,
}

/// The eventfd file object has two modes of operation:
/// 1) Counter: Write adds to the value and read returns the value while setting it to zero.
/// 2) Semaphore: Write adds one to the counter and read decrements it and returns 1.
/// In both cases, if the value is 0, the read blocks or returns EAGAIN.
/// See https://man7.org/linux/man-pages/man2/eventfd.2.html

struct EventFdInner {
    value: u64,
    wait_queue: WaitQueue,
}

pub struct EventFdFileObject {
    inner: Mutex<EventFdInner>,
    eventfd_type: EventFdType,
}

pub fn new_eventfd(
    kernel: &Kernel,
    value: u32,
    eventfd_type: EventFdType,
    blocking: bool,
) -> FileHandle {
    let open_flags = if blocking { OpenFlags::RDWR } else { OpenFlags::RDWR | OpenFlags::NONBLOCK };
    Anon::new_file(
        anon_fs(kernel),
        Box::new(EventFdFileObject {
            inner: Mutex::new(EventFdInner {
                value: value.into(),
                wait_queue: WaitQueue::default(),
            }),
            eventfd_type,
        }),
        open_flags,
    )
}

fn query_events_internal(inner: &EventFdInner) -> FdEvents {
    // TODO check for error and HUP events
    let mut events = FdEvents::empty();
    if inner.value > 0 {
        events |= FdEvents::POLLIN;
    }
    if inner.value < u64::MAX - 1 {
        events |= FdEvents::POLLOUT;
    }
    events
}

impl FileOps for EventFdFileObject {
    fd_impl_nonseekable!();

    fn write(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut written_data = [0; DATA_SIZE];
        if current_task.mm.read_all(data, &mut written_data)? < DATA_SIZE {
            return error!(EINVAL);
        }
        let add_value = u64::from_ne_bytes(written_data);
        if add_value == u64::MAX {
            return error!(EINVAL);
        }

        // The maximum value of the counter is u64::MAX - 1
        let mut inner = self.inner.lock();
        let headroom = u64::MAX - inner.value;
        if headroom < add_value {
            return error!(EAGAIN);
        }
        inner.value = inner.value + add_value;
        if inner.value > 0 {
            inner.wait_queue.notify_mask(FdEvents::POLLIN.mask());
        }
        Ok(DATA_SIZE)
    }

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if UserBuffer::get_total_length(data)? < DATA_SIZE {
            return error!(EINVAL);
        }

        let mut inner = self.inner.lock();
        if inner.value == 0 {
            return error!(EAGAIN);
        }

        let return_value = match self.eventfd_type {
            EventFdType::Counter => {
                let start_value = inner.value;
                inner.value = 0;
                start_value
            }
            EventFdType::Semaphore => {
                inner.value = inner.value - 1;
                1
            }
        };
        current_task.mm.write_all(data, &return_value.to_ne_bytes())?;
        inner.wait_queue.notify_mask(FdEvents::POLLOUT.mask());

        Ok(DATA_SIZE)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) {
        let mut inner = self.inner.lock();
        let present_events = query_events_internal(&inner);
        if events & present_events {
            waiter.wake_immediately(present_events.mask(), handler);
        } else {
            inner.wait_queue.wait_async_mask(waiter, events.mask(), handler);
        }
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        let inner = self.inner.lock();
        query_events_internal(&inner)
    }
}
