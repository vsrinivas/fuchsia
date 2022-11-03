// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::mm::MemoryAccessorExt;
use crate::signals::*;
use crate::task::*;
use crate::types::*;
use std::convert::TryInto;

use zerocopy::AsBytes;

pub struct SignalFd {
    mask: sigset_t,
}

impl SignalFd {
    pub fn new_file(current_task: &CurrentTask, mask: sigset_t, flags: u32) -> FileHandle {
        let mut open_flags = OpenFlags::RDONLY;
        if flags & SFD_NONBLOCK != 0 {
            open_flags |= OpenFlags::NONBLOCK;
        }
        Anon::new_file(current_task, Box::new(SignalFd { mask }), open_flags)
    }
}

impl FileOps for SignalFd {
    fileops_impl_nonseekable!();

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
                .write()
                .signals
                .take_next_where(|sig| sig.signal.is_in_set(self.mask))
                .ok_or_else(|| errno!(EAGAIN))?;
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
                SignalDetail::Raw { data } => {
                    // these offsets are taken from the gVisor offsets in the SignalInfo struct
                    // in //pkg/abi/linux/signal.go and the definition of __sifields in
                    // /usr/include/asm-generic/siginfo.h
                    siginfo.ssi_uid = u32::from_ne_bytes(data[4..8].try_into().unwrap());
                    siginfo.ssi_pid = u32::from_ne_bytes(data[0..4].try_into().unwrap());
                    siginfo.ssi_fd = i32::from_ne_bytes(data[8..12].try_into().unwrap());
                    siginfo.ssi_tid = u32::from_ne_bytes(data[0..4].try_into().unwrap());
                    siginfo.ssi_band = u32::from_ne_bytes(data[0..4].try_into().unwrap());
                    siginfo.ssi_overrun = u32::from_ne_bytes(data[4..8].try_into().unwrap());
                    siginfo.ssi_status = i32::from_ne_bytes(data[8..12].try_into().unwrap());
                    siginfo.ssi_int = i32::from_ne_bytes(data[8..12].try_into().unwrap());
                    siginfo.ssi_ptr = u64::from_ne_bytes(data[8..16].try_into().unwrap());
                    siginfo.ssi_addr = u64::from_ne_bytes(data[0..8].try_into().unwrap());
                    siginfo.ssi_syscall = i32::from_ne_bytes(data[8..12].try_into().unwrap());
                    siginfo.ssi_call_addr = u64::from_ne_bytes(data[0..8].try_into().unwrap());
                    siginfo.ssi_arch = u32::from_ne_bytes(data[12..16].try_into().unwrap());
                    siginfo.ssi_utime = u64::from_ne_bytes(data[12..20].try_into().unwrap());
                    siginfo.ssi_stime = u64::from_ne_bytes(data[20..28].try_into().unwrap());
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
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        // TODO(tbodt): The fact that so many of the wait_async methods have the same
        // wake_immediately call is a sign that maybe it should be factored out to a higher layer.
        let mut task_state = current_task.write();
        if task_state.signals.is_any_allowed_by_mask(!self.mask)
            && !options.contains(WaitAsyncOptions::EDGE_TRIGGERED)
        {
            waiter.wake_immediately(FdEvents::POLLIN.mask(), handler)
        } else {
            task_state.signals.signal_wait.wait_async_mask(waiter, events.mask(), handler)
        }
    }

    fn cancel_wait(&self, current_task: &CurrentTask, _waiter: &Waiter, key: WaitKey) {
        current_task.write().signals.signal_wait.cancel_wait(key);
    }

    fn query_events(&self, current_task: &CurrentTask) -> FdEvents {
        let mut events = FdEvents::empty();
        if current_task.read().signals.is_any_allowed_by_mask(!self.mask) {
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
