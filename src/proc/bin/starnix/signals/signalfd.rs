// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::signals::*;
use crate::task::*;
use crate::types::*;
use crate::{errno, error, fd_impl_nonseekable};
use std::sync::Arc;

use zerocopy::AsBytes;

pub struct SignalFd {
    mask: sigset_t,
}

impl SignalFd {
    pub fn new(kernel: &Kernel, mask: sigset_t) -> FileHandle {
        Anon::new_file(anon_fs(kernel), Box::new(SignalFd { mask }), OpenFlags::RDONLY)
    }
}

impl FileOps for SignalFd {
    fd_impl_nonseekable!();

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let data_len = UserBuffer::get_total_length(data)?;
        let mut buf = Vec::new();
        while buf.len() + std::mem::size_of::<signalfd_siginfo>() <= data_len {
            let signal = current_task
                .signals
                .write()
                .take_next_allowed_by_mask(!self.mask)
                .ok_or(errno!(EAGAIN))?;
            let mut siginfo = signalfd_siginfo {
                ssi_signo: signal.signal.number(),
                ssi_errno: signal.errno,
                ssi_code: signal.code,
                ..Default::default()
            };
            // Any future variants of SignalDetail need a match arm here that copies the relevant
            // fields into the signalfd_siginfo.
            match signal.detail {
                SignalDetail::None => {}
                SignalDetail::SigChld { pid, uid, status } => {
                    siginfo.ssi_pid = pid as u32;
                    siginfo.ssi_uid = uid;
                    siginfo.ssi_status = status;
                }
            }
            buf.extend_from_slice(siginfo.as_bytes());
        }
        current_task.mm.write_all(data, &buf)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) {
        // TODO(tbodt): The fact that so many of the wait_async methods have the same
        // wake_immediately call is a sign that maybe it should be factored out to a higher layer.
        let mut signals = current_task.signals.write();
        if signals.is_any_allowed_by_mask(!self.mask) {
            waiter.wake_immediately(FdEvents::POLLIN.mask(), handler);
        } else {
            signals.signalfd_wait.wait_async_mask(waiter, events.mask(), handler);
        }
    }

    fn query_events(&self, current_task: &CurrentTask) -> FdEvents {
        let mut events = FdEvents::empty();
        let signals = current_task.signals.read();
        if signals.is_any_allowed_by_mask(!self.mask) {
            events |= FdEvents::POLLIN;
        }
        events
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}
