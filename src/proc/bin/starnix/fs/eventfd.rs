// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::task::*;
use crate::types::*;
use parking_lot::Mutex;

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
pub struct EventFdFileObject {
    value: Mutex<u64>,
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
        Box::new(EventFdFileObject { value: Mutex::new(value.into()), eventfd_type }),
        open_flags,
    )
}

impl FileOps for EventFdFileObject {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut written_data = [0; DATA_SIZE];
        if task.mm.read_all(data, &mut written_data)? < DATA_SIZE {
            return error!(EINVAL);
        }
        let add_value = u64::from_ne_bytes(written_data);
        if add_value == u64::MAX {
            return error!(EINVAL);
        }

        // The maximum value of the counter is u64::MAX - 1
        let mut value = self.value.lock();
        let headroom = u64::MAX - *value;
        if headroom < add_value {
            return error!(EAGAIN);
        }
        *value = *value + add_value;
        Ok(DATA_SIZE)
    }

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        if UserBuffer::get_total_length(data) < DATA_SIZE {
            return error!(EINVAL);
        }

        let mut value = self.value.lock();
        if *value == 0 {
            return error!(EAGAIN);
        }

        let return_value = match self.eventfd_type {
            EventFdType::Counter => {
                let start_value = *value;
                *value = 0;
                start_value
            }
            EventFdType::Semaphore => {
                *value = *value - 1;
                1
            }
        };
        task.mm.write_all(data, &return_value.to_ne_bytes())?;

        Ok(DATA_SIZE)
    }
}
