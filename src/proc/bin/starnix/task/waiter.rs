// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use crate::logging::*;
use crate::types::*;
use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

pub type WaitHandler = Box<dyn FnOnce(zx::Signals) + Send + Sync>;

/// A type that can put a thread to sleep waiting for a condition.
pub struct Waiter {
    /// The underlying Zircon port that the thread sleeps in.
    port: zx::Port,
    key_map: Mutex<HashMap<u64, WaitHandler>>, // the key 0 is reserved for 'no handler'
    next_key: AtomicU64,
}

impl Waiter {
    /// Create a new waiter object.
    pub fn new() -> Arc<Waiter> {
        Arc::new(Waiter {
            port: zx::Port::create().map_err(impossible_error).unwrap(),
            key_map: Mutex::new(HashMap::new()),
            next_key: AtomicU64::new(1),
        })
    }

    /// Wait until the waiter is woken up.
    ///
    /// If the wait is interrupted (see interrupt), this function returns
    /// EINTR.
    pub fn wait(&self) -> Result<(), Errno> {
        self.wait_until(zx::Time::INFINITE)
    }

    /// Wait until the given deadline has passed or the waiter is woken up.
    ///
    /// If the wait is interrupted (seee interrupt), this function returns
    /// EINTR.
    pub fn wait_until(&self, deadline: zx::Time) -> Result<(), Errno> {
        match self.port.wait(deadline) {
            Ok(packet) => match packet.status() {
                zx::sys::ZX_OK => {
                    let contents = packet.contents();
                    match contents {
                        zx::PacketContents::SignalOne(sigpkt) => {
                            let key = packet.key();
                            if let Some(callback) = self.key_map.lock().remove(&key) {
                                callback(sigpkt.observed());
                            }
                        }
                        zx::PacketContents::User(_) => {}
                        _ => {
                            return error!(EBADMSG);
                        }
                    }
                    Ok(())
                }
                // TODO make a match arm for this and return EBADMSG by default
                _ => {
                    return error!(EINTR);
                }
            },
            Err(zx::Status::TIMED_OUT) => Ok(()),
            Err(errno) => Err(impossible_error(errno)),
        }
    }

    /// Establish an asynchronous wait for the signals on the given handle,
    /// optionally running a FnOnce.
    pub fn wake_and_call_on(
        &self,
        handle: &dyn zx::AsHandleRef,
        signals: zx::Signals,
        handler: Option<WaitHandler>,
    ) -> Result<(), zx::Status> {
        let key = handler
            .map(|callback| {
                let key = self.next_key.fetch_add(1, Ordering::Relaxed);
                // TODO - find a better reaction to wraparound
                assert!(key != 0, "bad key from u64 wraparound");
                assert!(
                    self.key_map.lock().insert(key, callback).is_none(),
                    "unexpected callback already present for key {}",
                    key
                );
                key
            })
            .unwrap_or(0);
        handle.wait_async_handle(&self.port, key, signals, zx::WaitAsyncOpts::empty())
    }

    /// Wake up the waiter.
    ///
    /// This function is called before the waiter goes to sleep, the waiter
    /// will wake up immediately upon attempting to go to sleep.
    pub fn wake(&self) {
        self.queue_user_packet(zx::sys::ZX_OK);
    }

    /// Interrupt the waiter.
    ///
    /// Used to break the waiter out of its sleep, for example to deliver an
    /// async signal. The wait operation will return EINTR, and unwind until
    /// the thread can process the async signal.
    #[allow(dead_code)]
    pub fn interrupt(&self) {
        self.queue_user_packet(zx::sys::ZX_ERR_CANCELED);
    }

    /// Queue a packet to the underlying Zircon port, which will cause the
    /// waiter to wake up.
    fn queue_user_packet(&self, status: i32) {
        let user_packet = zx::UserPacket::from_u8_array([0u8; 32]);
        let packet = zx::Packet::from_user_packet(0, status, user_packet);
        self.port.queue(&packet).map_err(impossible_error).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::fuchsia::*;
    use crate::fs::FdEvents;
    use crate::syscalls::SyscallContext;
    use fuchsia_async as fasync;

    use crate::testing::*;

    static INIT_VAL: u64 = 0;
    static FINAL_VAL: u64 = 42;
    static COUNTER: AtomicU64 = AtomicU64::new(INIT_VAL);
    static WRITE_COUNT: AtomicU64 = AtomicU64::new(0);

    #[fasync::run_singlethreaded(test)]
    async fn test_async_wait_exec() {
        let (kernel, task_owner) = create_kernel_and_task();
        let task = &task_owner.task;
        let ctx = SyscallContext::new(&task_owner.task);
        let (local_socket, remote_socket) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let pipe = create_fuchsia_pipe(&kernel, remote_socket).unwrap();

        const MEM_SIZE: usize = 1024;
        let proc_mem = map_memory(&ctx, UserAddress::default(), MEM_SIZE as u64);
        let proc_read_buf: Box<[UserBuffer]> =
            Box::new([UserBuffer { address: proc_mem, length: MEM_SIZE }]);

        let test_string = Arc::new("hello startnix".to_string());
        let report_packet = |observed: zx::Signals| {
            assert!(observed.contains(zx::Signals::CHANNEL_READABLE));
            COUNTER.store(FINAL_VAL, Ordering::Relaxed);
        };
        let waiter = Waiter::new();
        pipe.wait_async(&waiter, FdEvents::POLLIN, Some(Box::new(report_packet)));
        let test_string_clone = test_string.clone();
        let thread = std::thread::spawn(move || {
            let test_data = test_string_clone.as_bytes();
            let no_written = local_socket.write(&test_data).unwrap();
            assert_eq!(0, WRITE_COUNT.fetch_add(no_written as u64, Ordering::Relaxed));
            assert_eq!(no_written, test_data.len());
        });

        // this code would block on failure
        assert_eq!(INIT_VAL, COUNTER.load(Ordering::Relaxed));
        waiter.wait().unwrap();
        let _ = thread.join();
        assert_eq!(FINAL_VAL, COUNTER.load(Ordering::Relaxed));

        let read_size = pipe.read(&task, &proc_read_buf).unwrap();
        let mut read_mem = [0u8; MEM_SIZE];
        task.mm.read_all(&proc_read_buf, &mut read_mem).unwrap();

        let no_written = WRITE_COUNT.load(Ordering::Relaxed);
        assert_eq!(no_written, read_size as u64);

        let read_mem_valid = &read_mem[0..read_size];
        assert_eq!(*&read_mem_valid, test_string.as_bytes());
    }
}
