// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use crate::fs::FdEvents;
use crate::logging::*;
use crate::task::CurrentTask;
use crate::types::Errno;
use crate::types::*;
use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

pub type SignalHandler = Box<dyn FnOnce(zx::Signals) + Send + Sync>;
pub type EventHandler = Box<dyn FnOnce(FdEvents) + Send + Sync>;

pub enum WaitCallback {
    SignalHandler(SignalHandler),
    EventHandler(EventHandler),
}

pub struct WaitKey {
    key: u64,
}

impl WaitKey {
    /// an empty key means no associated handler
    pub fn empty() -> WaitKey {
        WaitKey { key: 0 }
    }
}

impl WaitCallback {
    pub fn none() -> EventHandler {
        Box::new(|_| {})
    }
}

/// A type that can put a thread to sleep waiting for a condition.
pub struct Waiter {
    /// The underlying Zircon port that the thread sleeps in.
    port: zx::Port,
    key_map: Mutex<HashMap<u64, WaitCallback>>, // the key 0 is reserved for 'no handler'
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
    pub fn wait(self: &Arc<Self>, current_task: &CurrentTask) -> Result<(), Errno> {
        self.wait_until(current_task, zx::Time::INFINITE)
    }

    /// Wait until the given deadline has passed or the waiter is woken up.
    ///
    /// If the wait is interrupted (seee interrupt), this function returns
    /// EINTR.
    pub fn wait_until(
        self: &Arc<Self>,
        current_task: &CurrentTask,
        deadline: zx::Time,
    ) -> Result<(), Errno> {
        {
            let mut signal_state = current_task.signals.write();
            if signal_state.is_any_pending() {
                return error!(EINTR);
            }
            signal_state.waiter = Some(Arc::clone(self));
        }

        scopeguard::defer! {
            let mut signal_state = current_task.signals.write();
            assert!(Arc::ptr_eq(signal_state.waiter.as_ref().unwrap(), self), "SignalState waiter changed while waiting!");
            signal_state.waiter = None;
        }

        self.wait_kernel(deadline)
    }

    /// Waits until the given deadline has passed or the waiter is woken up.
    ///
    /// This method allows the kernel to wait without a specific task at hand.
    pub fn wait_kernel(self: &Arc<Self>, deadline: zx::Time) -> Result<(), Errno> {
        match self.port.wait(deadline) {
            Ok(packet) => match packet.status() {
                zx::sys::ZX_OK => {
                    let contents = packet.contents();
                    match contents {
                        zx::PacketContents::SignalOne(sigpkt) => {
                            let key = packet.key();

                            if let Some(callback) = self.key_map.lock().remove(&key) {
                                match callback {
                                    WaitCallback::SignalHandler(handler) => {
                                        handler(sigpkt.observed())
                                    }
                                    WaitCallback::EventHandler(_) => {
                                        panic!("wrong type of handler called")
                                    }
                                }
                            }
                        }
                        zx::PacketContents::User(usrpkt) => {
                            let observed = usrpkt.as_u8_array();
                            let mut mask_bytes = [0u8; 4];
                            mask_bytes[..4].copy_from_slice(&observed[..4]);
                            let events = FdEvents::from(u32::from_ne_bytes(mask_bytes));
                            let key = packet.key();
                            if let Some(callback) = self.key_map.lock().remove(&key) {
                                match callback {
                                    WaitCallback::EventHandler(handler) => {
                                        assert!(events != FdEvents::empty());
                                        handler(events)
                                    }
                                    WaitCallback::SignalHandler(_) => {
                                        panic!("wrong type of handler called")
                                    }
                                }
                            }
                        }
                        _ => return error!(EBADMSG),
                    }
                    Ok(())
                }
                // TODO make a match arm for this and return EBADMSG by default
                _ => error!(EINTR),
            },
            Err(zx::Status::TIMED_OUT) => error!(ETIMEDOUT),
            Err(errno) => Err(impossible_error(errno)),
        }
    }

    fn register_callback(&self, callback: WaitCallback) -> u64 {
        let key = self.next_key.fetch_add(1, Ordering::Relaxed);
        // TODO - find a better reaction to wraparound
        assert!(key != 0, "bad key from u64 wraparound");
        assert!(
            self.key_map.lock().insert(key, callback).is_none(),
            "unexpected callback already present for key {}",
            key
        );
        key
    }

    pub fn wake_immediately(&self, event_mask: u32, handler: EventHandler) -> WaitKey {
        let callback = WaitCallback::EventHandler(handler);
        let key = WaitKey { key: self.register_callback(callback) };
        let mut packet_data = [0u8; 32];
        packet_data[..4].copy_from_slice(&event_mask.to_ne_bytes()[..4]);

        self.queue_user_packet_data(&key, zx::sys::ZX_OK, packet_data);
        key
    }

    /// Establish an asynchronous wait for the signals on the given handle,
    /// optionally running a FnOnce.
    pub fn wake_on_signals(
        &self,
        handle: &dyn zx::AsHandleRef,
        signals: zx::Signals,
        handler: SignalHandler,
    ) -> Result<(), zx::Status> {
        let callback = WaitCallback::SignalHandler(handler);
        let key = self.register_callback(callback);
        handle.wait_async_handle(&self.port, key, signals, zx::WaitAsyncOpts::empty())
    }

    fn wake_on_events(&self, handler: EventHandler) -> WaitKey {
        let callback = WaitCallback::EventHandler(handler);
        let key = self.register_callback(callback);
        WaitKey { key }
    }

    fn queue_events(&self, key: &WaitKey, event_mask: u32) {
        let mut packet_data = [0u8; 32];
        packet_data[..4].copy_from_slice(&event_mask.to_ne_bytes()[..4]);

        self.queue_user_packet_data(key, zx::sys::ZX_OK, packet_data);
    }

    /// Interrupt the waiter.
    ///
    /// Used to break the waiter out of its sleep, for example to deliver an
    /// async signal. The wait operation will return EINTR, and unwind until
    /// the thread can process the async signal.
    pub fn interrupt(&self) {
        self.queue_user_packet(zx::sys::ZX_ERR_CANCELED);
    }

    /// Queue a packet to the underlying Zircon port, which will cause the
    /// waiter to wake up.
    fn queue_user_packet(&self, status: i32) {
        let key = WaitKey::empty();
        self.queue_user_packet_data(&key, status, [0u8; 32]);
    }

    fn queue_user_packet_data(&self, key: &WaitKey, status: i32, packet_data: [u8; 32]) {
        let user_packet = zx::UserPacket::from_u8_array(packet_data);
        let packet = zx::Packet::from_user_packet(key.key, status, user_packet);
        self.port.queue(&packet).map_err(impossible_error).unwrap();
    }
}

/// A list of waiters waiting for some event.
///
/// For events that are generated inside Starnix, we walk the wait queue
/// on the thread that triggered the event to notify the waiters that the event
/// has occurred. The waiters will then wake up on their own thread to handle
/// the event.
#[derive(Default)]
pub struct WaitQueue {
    /// The list of waiters.
    waiters: Vec<WaitEntry>,
}

/// An entry in a WaitQueue.
struct WaitEntry {
    /// The waiter that is waking for the FdEvent.
    waiter: Arc<Waiter>,

    /// The bitmask that the waiter is waiting for.
    events: u32,

    /// Whether the waiter wishes to remain in the WaitQueue after one of
    /// the events that the waiter is waiting for occurs.
    persistent: bool,

    /// key for cancelling and queueing events
    key: WaitKey,
}

impl WaitQueue {
    /// Establish a wait for the given events.
    ///
    /// The waiter will be notified when an event matching the events mask
    /// occurs.
    ///
    /// This function does not actually block the waiter. To block the waiter,
    /// call the "wait" function on the waiter.
    pub fn wait_async_mask(&mut self, waiter: &Arc<Waiter>, events: u32, handler: EventHandler) {
        let key = waiter.wake_on_events(handler);
        self.waiters.push(WaitEntry { waiter: Arc::clone(waiter), events, persistent: false, key });
    }

    pub fn wait_async(&mut self, waiter: &Arc<Waiter>) {
        self.wait_async_mask(waiter, u32::MAX, WaitCallback::none())
    }

    /// Notify any waiters that the given events have occurred.
    ///
    /// Walks the wait queue and wakes each waiter that is waiting on an
    /// event that matches the given mask. Persistent waiters remain in the
    /// list. Non-persistent waiters are removed.
    ///
    /// The waiters will wake up on their own threads to handle these events.
    /// They are not called synchronously by this function.
    pub fn notify_mask_count(&mut self, events: u32, mut limit: usize) {
        self.waiters = std::mem::take(&mut self.waiters)
            .into_iter()
            .filter(|entry| {
                if limit > 0 && (entry.events & events) != 0 {
                    entry.waiter.queue_events(&entry.key, events);
                    limit -= 1;
                    return entry.persistent;
                }
                return true;
            })
            .collect();
    }

    pub fn notify_mask(&mut self, events: u32) {
        self.notify_mask_count(events, usize::MAX)
    }

    pub fn notify_events(&mut self, events: FdEvents) {
        self.notify_mask(events.mask())
    }

    pub fn notify_count(&mut self, limit: usize) {
        self.notify_mask_count(u32::MAX, limit)
    }

    pub fn notify_all(&mut self) {
        self.notify_count(usize::MAX)
    }

    pub fn transfer(&mut self, other: &mut WaitQueue) {
        for entry in other.waiters.drain(..) {
            self.waiters.push(entry);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::fuchsia::*;
    use crate::fs::FdEvents;
    use crate::types::UserBuffer;
    use fuchsia_async as fasync;

    use crate::testing::*;

    static INIT_VAL: u64 = 0;
    static FINAL_VAL: u64 = 42;

    #[fasync::run_singlethreaded(test)]
    async fn test_async_wait_exec() {
        static COUNTER: AtomicU64 = AtomicU64::new(INIT_VAL);
        static WRITE_COUNT: AtomicU64 = AtomicU64::new(0);

        let (kernel, current_task) = create_kernel_and_task();
        let (local_socket, remote_socket) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let pipe = create_fuchsia_pipe(&kernel, remote_socket).unwrap();

        const MEM_SIZE: usize = 1024;
        let proc_mem = map_memory(&current_task, UserAddress::default(), MEM_SIZE as u64);
        let proc_read_buf = [UserBuffer { address: proc_mem, length: MEM_SIZE }];

        let test_string = "hello startnix".to_string();
        let report_packet: EventHandler = Box::new(|observed: FdEvents| {
            assert!(FdEvents::POLLIN & observed);
            COUNTER.store(FINAL_VAL, Ordering::Relaxed);
        });
        let waiter = Waiter::new();
        pipe.wait_async(&current_task, &waiter, FdEvents::POLLIN, report_packet);
        let test_string_clone = test_string.clone();

        let thread = std::thread::spawn(move || {
            let test_data = test_string_clone.as_bytes();
            let no_written = local_socket.write(&test_data).unwrap();
            assert_eq!(0, WRITE_COUNT.fetch_add(no_written as u64, Ordering::Relaxed));
            assert_eq!(no_written, test_data.len());
        });

        // this code would block on failure
        assert_eq!(INIT_VAL, COUNTER.load(Ordering::Relaxed));
        waiter.wait(&current_task).unwrap();
        let _ = thread.join();
        assert_eq!(FINAL_VAL, COUNTER.load(Ordering::Relaxed));

        let read_size = pipe.read(&current_task, &proc_read_buf).unwrap();
        let mut read_mem = [0u8; MEM_SIZE];
        current_task.mm.read_all(&proc_read_buf, &mut read_mem).unwrap();

        let no_written = WRITE_COUNT.load(Ordering::Relaxed);
        assert_eq!(no_written, read_size as u64);

        let read_mem_valid = &read_mem[0..read_size];
        assert_eq!(*&read_mem_valid, test_string.as_bytes());
    }

    #[test]
    fn test_wait_queue() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mut queue = WaitQueue::default();

        let waiter0 = Waiter::new();
        let waiter1 = Waiter::new();
        let waiter2 = Waiter::new();

        queue.wait_async(&waiter0);
        queue.wait_async(&waiter1);
        queue.wait_async(&waiter2);

        queue.notify_count(2);
        assert!(waiter0.wait_until(&current_task, zx::Time::ZERO).is_ok());
        assert!(waiter1.wait_until(&current_task, zx::Time::ZERO).is_ok());
        assert!(waiter2.wait_until(&current_task, zx::Time::ZERO).is_err());

        queue.notify_all();
        assert!(waiter0.wait_until(&current_task, zx::Time::ZERO).is_err());
        assert!(waiter1.wait_until(&current_task, zx::Time::ZERO).is_err());
        assert!(waiter2.wait_until(&current_task, zx::Time::ZERO).is_ok());

        queue.notify_count(3);
        assert!(waiter0.wait_until(&current_task, zx::Time::ZERO).is_err());
        assert!(waiter1.wait_until(&current_task, zx::Time::ZERO).is_err());
        assert!(waiter2.wait_until(&current_task, zx::Time::ZERO).is_err());
    }

    #[test]
    fn test_wait_queue_mask() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mut queue = WaitQueue::default();

        let waiter0 = Waiter::new();
        let waiter1 = Waiter::new();
        let waiter2 = Waiter::new();

        queue.wait_async_mask(&waiter0, 0x13, WaitCallback::none());
        queue.wait_async_mask(&waiter1, 0x11, WaitCallback::none());
        queue.wait_async_mask(&waiter2, 0x12, WaitCallback::none());

        queue.notify_mask_count(0x2, 2);
        assert!(waiter0.wait_until(&current_task, zx::Time::ZERO).is_ok());
        assert!(waiter1.wait_until(&current_task, zx::Time::ZERO).is_err());
        assert!(waiter2.wait_until(&current_task, zx::Time::ZERO).is_ok());

        queue.notify_mask_count(0x1, usize::MAX);
        assert!(waiter0.wait_until(&current_task, zx::Time::ZERO).is_err());
        assert!(waiter1.wait_until(&current_task, zx::Time::ZERO).is_ok());
        assert!(waiter2.wait_until(&current_task, zx::Time::ZERO).is_err());
    }
}
