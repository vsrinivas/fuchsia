// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{executor, Async, Future};
use parking_lot::{Mutex, Condvar};
use slab::Slab;
use zx;

use atomic_future::{AtomicFuture, AttemptPoll, TaskFut};

use std::fmt;
use std::mem;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::{usize, u64};

const EMPTY_WAKEUP_ID: u64 = u64::MAX;

/// A trait for handling the arrival of a packet on a `zx::Port`.
///
/// This trait should be implemented by users who wish to write their own
/// types which receive asynchronous notifications from a `zx::Port`.
/// Implementors of this trait generally contain a `futures::task::AtomicTask` which
/// is used to wake up the task which can make progress due to the arrival of
/// the packet.
///
/// `PacketReceiver`s should be registered with a `Core` using the
/// `register_receiver` method on `Core`, `Handle`, or `Remote`.
/// Upon registration, users will receive a `ReceiverRegistration`
/// which provides `key` and `port` methods. These methods can be used to wait on
/// asynchronous signals.
pub trait PacketReceiver: Send + Sync + 'static {
    /// Receive a packet when one arrives.
    fn receive_packet(&self, packet: zx::Packet);
}

/// A registration of a `PacketReceiver`.
/// When dropped, it will automatically deregister the `PacketReceiver`.
// NOTE: purposefully does not implement `Clone`.
#[derive(Debug)]
pub struct ReceiverRegistration<T: PacketReceiver> {
    receiver: Arc<T>,
    ehandle: EHandle,
    key: u64,
}

impl<T> ReceiverRegistration<T> where T: PacketReceiver {
    /// The key with which `Packet`s destined for this receiver should be sent on the `zx::Port`.
    pub fn key(&self) -> u64 {
        self.key
    }

    /// The internal `PacketReceiver`.
    pub fn receiver(&self) -> &T {
        &*self.receiver
    }


    /// The `zx::Port` on which packets destined for this `PacketReceiver` should be queued.
    pub fn port(&self) -> &zx::Port {
        self.ehandle.port()
    }
}

impl<T> Drop for ReceiverRegistration<T> where T: PacketReceiver {
    fn drop(&mut self) {
        self.ehandle.deregister_receiver(self.key);
    }
}

/// A port-based executor for Fuchsia OS.
// NOTE: intentionally does not implement `Clone`.
pub struct Executor {
    inner: Arc<Inner>,
}

impl fmt::Debug for Executor {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Executor")
         .field("port", &self.inner.port)
         .finish()
    }
}

impl Executor {
    /// Creates a new executor.
    pub fn new() -> Result<Self, zx::Status> {
        Ok(Executor {
            inner: Arc::new(Inner {
                port: zx::Port::create()?,
                done: AtomicBool::new(false),
                threads: Mutex::new(Vec::new()),
                receivers: Mutex::new(Slab::new()),
                futures: Mutex::new(Slab::new()),
            })
        })
    }

    /// Returns a handle to the executor.
    pub fn ehandle(&self) -> EHandle {
        EHandle {
            inner: self.inner.clone()
        }
    }

    /// Run a single future to completion on a single thread.
    // Takes `&mut self` to ensure that only one thread-manager is running at a time.
    pub fn run_singlethreaded<F>(&mut self, future: F) -> Result<F::Item, F::Error>
        where F: Future
    {
        let mut main_future = executor::spawn(future);

        // Since there are no other threads running, we don't have to use the EMPTY_WAKEUP_ID,
        // so instead we save it for use as the main task wakeup id.
        let main_future_wakeup_id = EMPTY_WAKEUP_ID as usize;
        let mut res = main_future.poll_future_notify(&self.inner, main_future_wakeup_id);

        loop {
            if let Async::Ready(res) = res? {
                return Ok(res);
            }

            let packet = match self.inner.port.wait(zx::Time::INFINITE) {
                Ok(packet) => packet,
                Err(status) => {
                    panic!("Error calling port wait: {:?}", status);
                }
            };

            if packet.key() == EMPTY_WAKEUP_ID {
                res = main_future.poll_future_notify(&self.inner, main_future_wakeup_id);
            } else {
                res = Ok(Async::NotReady);
                let (key, kind) = notify_key_to_key(packet.key() as usize);
                match kind {
                    IdKind::Future => {
                        poll_future(&self.inner, key);
                    },
                    IdKind::Receiver => self.inner.deliver_packet(key, packet),
                }
            }
        }
    }

    /// Run a single future to completion using multiple threads.
    // Takes `&mut self` to ensure that only one thread-manager is running at a time.
    pub fn run<F>(&mut self, future: F, num_threads: usize)
        -> Result<F::Item, F::Error>
        where F: Future + Send + 'static,
              Result<F::Item, F::Error>: Send + 'static,
    {
        let pair = Arc::new((Mutex::new(None), Condvar::new()));
        let pair2 = pair.clone();

        // Spawn a future which will set the result upon completion.
        self.inner.spawn(future.then(move |fut_result| {
            let &(ref lock, ref cvar) = &*pair2;
            let mut result = lock.lock();
            *result = Some(fut_result);
            cvar.notify_one();
            Ok(())
        }));

        // Start worker threads
        self.inner.done.store(false, Ordering::SeqCst);
        self.create_worker_threads(num_threads);

        // Wait until the signal the future has completed.
        let &(ref lock, ref cvar) = &*pair;
        let mut result = lock.lock();
        while result.is_none() {
            cvar.wait(&mut result);
        }

        // Spin down worker threads
        self.inner.done.store(true, Ordering::SeqCst);
        self.join_all();

        // Unwrap is fine because of the check to `is_none` above.
        result.take().unwrap()
    }

    /// Add `num_workers` worker threads to the executor's thread pool.
    fn create_worker_threads(&self, num_workers: usize) {
        let mut threads = self.inner.threads.lock();
        for _ in 0..num_workers {
            threads.push(self.new_worker());
        }
    }

    fn join_all(&self) {
        let mut threads = self.inner.threads.lock();

        // Send a user packet to wake up all the threads
        for _thread in threads.iter() {
            let up = zx::UserPacket::from_u8_array([0; 32]);
            let packet = zx::Packet::from_user_packet(EMPTY_WAKEUP_ID, zx::sys::ZX_OK, up);
            if let Err(e) = self.inner.port.queue(&packet) {
                // TODO: logging
                eprintln!("Failed to queue thread-wakeup notify in port: {:?}", e);
            }
        }

        // Join the worker threads
        for thread in threads.drain(..) {
            thread.join().expect("Couldn't join worker thread.");
        }
    }

    fn new_worker(&self) -> thread::JoinHandle<()> {
        let inner = self.inner.clone();
        thread::spawn(move || Self::worker_lifecycle(inner))
    }

    fn worker_lifecycle(inner: Arc<Inner>) {
        loop {
            if inner.done.load(Ordering::SeqCst) {
                return;
            }

            let packet = match inner.port.wait(zx::Time::INFINITE) {
                Ok(packet) => packet,
                Err(status) => {
                    // TODO: logging, awaken main thread, signal error somehow.
                    // Maybe retry on a timeout?
                    eprintln!("Error calling port wait: {:?}", status);
                    return;
                }
            };

            if packet.key() != EMPTY_WAKEUP_ID {
                let (key, kind) = notify_key_to_key(packet.key() as usize);
                match kind {
                    IdKind::Future => poll_future(&inner, key),
                    IdKind::Receiver => inner.deliver_packet(key, packet),
                }
            }
        }
    }
}

impl Drop for Executor {
    fn drop(&mut self) {
        self.inner.done.store(true, Ordering::SeqCst);

        // Wake the threads so they can kill themselves.
        self.join_all();

        // Drop all of the futures
        self.inner.futures.lock().clear();
        self.inner.receivers.lock().clear();
    }
}

/// A handle to an executor.
#[derive(Clone)]
pub struct EHandle {
    inner: Arc<Inner>,
}

impl fmt::Debug for EHandle {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("EHandle")
         .field("port", &self.inner.port)
         .finish()
    }
}

impl EHandle {
    /// Get a reference to the Fuchsia `zx::Port` being used to listen for events.
    pub fn port(&self) -> &zx::Port {
        &self.inner.port
    }

    /// Spawn a new future on the executor.
    pub fn spawn<F>(&self, future: F)
        where F: TaskFut + 'static
    {
        self.inner.spawn(future);
    }

    /// Registers a `PacketReceiver` with the executor and returns a registration.
    /// The `PacketReceiver` will be deregistered when the `Registration` is dropped.
    pub fn register_receiver<T>(&self, receiver: Arc<T>) -> ReceiverRegistration<T>
        where T: PacketReceiver
    {
        let key = self.inner.receivers.lock().insert(receiver.clone());
        let notify_key = key_to_notify_key(key, IdKind::Receiver);

        ReceiverRegistration {
            ehandle: self.clone(),
            key: notify_key as u64,
            receiver,
        }
    }

    fn deregister_receiver(&self, key: u64) {
        self.inner.receivers.lock().remove(notify_key_to_key(key as usize).0);
    }
}

struct Inner {
    port: zx::Port,
    done: AtomicBool,
    threads: Mutex<Vec<thread::JoinHandle<()>>>,
    receivers: Mutex<Slab<Arc<PacketReceiver>>>,
    futures: Mutex<Slab<Arc<AtomicFuture<TaskFut<Item = (), Error = ()>>>>>,
}

impl Inner {
    fn spawn<F>(&self, future: F)
        where F: TaskFut + 'static
    {
        let arc_future = Arc::new(AtomicFuture::new(future));
        let key = self.futures.lock().insert(arc_future);

        // Schedule the future to be run once on startup.
        self.notify_future(key);
    }

    fn notify_future(&self, key: usize) {
        let up = zx::UserPacket::from_u8_array([0; 32]);
        let packet = zx::Packet::from_user_packet(key as u64, 0 /* status??? */, up);
        if let Err(e) = self.port.queue(&packet) {
            // TODO: logging
            eprintln!("Failed to queue notify in port: {:?}", e);
        }
    }

    fn deliver_packet(&self, key: usize, packet: zx::Packet) {
        let receiver = match self.receivers.lock().get(key) {
            // Clone the `Arc` so that we don't hold the lock
            // any longer than absolutely necessary.
            // The `receive_packet` impl may be arbitrarily complex.
            Some(receiver) => receiver.clone(),
            None => return,
        };
        receiver.receive_packet(packet);
    }
}

fn poll_future(inner: &Arc<Inner>, key: usize) {
    let future = {
        let locked = inner.futures.lock();
        if let Some(future_arc) = locked.get(key) {
            future_arc.clone()
        } else {
            return
        }
    };

    if AttemptPoll::IFinished == future.try_poll(
        inner, key_to_notify_key(key, IdKind::Future))
    {
        let mut locked = inner.futures.lock();
        let future_to_drop = locked.remove(key);

        // Let go of the lock before we do the drop work to minimize lock usage.
        mem::drop(locked);
        mem::drop(future_to_drop);
    }
}

impl executor::Notify for Inner {
    fn notify(&self, key: usize) {
        self.notify_future(key);
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
enum IdKind {
    Future,
    Receiver,
}

#[inline(always)]
fn usize_high_bit() -> usize {
    1usize << (usize::MAX.count_ones() - 1)
}

// Keys are stored with the highest bit indicating whether it's a Future or a PacketReceiver,
// and with the remaining bits storing the ID.

fn notify_key_to_key(notify_key: usize) -> (usize, IdKind) {
    (
        notify_key  & !usize_high_bit(),
        match notify_key & usize_high_bit() {
            0 => IdKind::Future,
            _ => IdKind::Receiver,
        }
    )
}

fn key_to_notify_key(key: usize, kind: IdKind) -> usize {
    if key & usize_high_bit() != 0 {
        panic!("ID flowed into usize top bit");
    }
    key | match kind {
        IdKind::Future => 0,
        IdKind::Receiver => usize_high_bit(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_token_can_store_kind() {
        for key in 0..1000 {
            for &kind in [IdKind::Future, IdKind::Receiver].iter() {
                let notify_key = key_to_notify_key(key, kind);
                let (out_key, out_kind) = notify_key_to_key(notify_key);
                assert_eq!((key, kind), (out_key, out_kind));
            }
        }
    }
}
