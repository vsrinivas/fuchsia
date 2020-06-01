// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::target,
    anyhow::{Context, Error},
    async_trait::async_trait,
    futures::channel::mpsc,
    futures::future,
    futures::lock::Mutex,
    futures::prelude::*,
    std::cmp::Eq,
    std::hash::Hash,
    std::net::SocketAddr,
    std::sync::Arc,
};

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub struct TargetInfo {
    pub nodename: String,
    pub addresses: Vec<target::TargetAddr>,
}

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum WireTrafficType {
    // It's simpler to leave this here than to sprinkle a few dozen linux-only
    // invocations throughout the daemon code.
    #[allow(dead_code)]
    Mdns(TargetInfo),
    Fastboot(TargetInfo),
}

/// Encapsulates an event that occurs on the daemon.
#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum DaemonEvent {
    WireTraffic(WireTrafficType),
    OvernetPeer(u64),
    // TODO(awdavies): Stale target event, target shutdown event, etc.
}

pub trait TryIntoTargetInfo: Sized {
    type Error;

    /// Attempts, given a source socket address, to determine whether the
    /// received message was from a Fuchsia target, and if so, what kind. Attempts
    /// to fill in as much information as possible given the message, consuming
    /// the underlying object in the process.
    fn try_into_target_info(self, src: SocketAddr) -> Result<TargetInfo, Self::Error>;
}

pub trait EventTrait: Sized + Hash + Clone + Eq {}
impl<T> EventTrait for T where T: Sized + Hash + Clone + Eq {}

/// Implements a general event handler for any inbound events.
#[async_trait]
pub trait EventHandler<T: EventTrait>: Send + Sync {
    async fn on_event(&self, event: T);
}

type Handlers<T> = Arc<Mutex<Vec<Arc<dyn EventHandler<T>>>>>;

#[derive(Clone)]
pub struct Queue<T> {
    inner_tx: mpsc::Sender<T>,
    handlers: Handlers<T>,
}

pub struct Processor<T> {
    inner_rx: Option<mpsc::Receiver<T>>,
    handlers: Handlers<T>,
}

pub fn new_queue<T>() -> (Queue<T>, Processor<T>)
where
    T: EventTrait,
{
    let (inner_tx, inner_rx) = mpsc::channel::<T>(4096);
    let handlers = Arc::new(Mutex::new(Vec::<Arc<dyn EventHandler<T>>>::new()));
    (
        Queue::<T> { inner_tx, handlers: handlers.clone() },
        Processor::<T> { inner_rx: Some(inner_rx), handlers: handlers.clone() },
    )
}

impl<T: EventTrait> Queue<T> {
    pub async fn add_handler(&self, handler: impl EventHandler<T> + 'static) {
        self.handlers
            .lock()
            .then(move |mut l| {
                l.push(Arc::new(handler));
                future::ready(())
            })
            .await;
    }

    pub async fn push(&mut self, event: T) -> Result<(), Error> {
        self.inner_tx.send(event).await.context("sending event")
    }
}

impl<T> Processor<T>
where
    T: EventTrait,
{
    async fn notify_handlers(&self, event: T) {
        let handlers = self.handlers.lock().await;
        future::join_all(handlers.iter().map(|handler| handler.on_event(event.clone()))).await;
    }

    /// Consumes the processor and then runs until all instances of the Queue are closed.
    pub async fn process(mut self) {
        let rx = self.inner_rx.take().expect("process should only ever be called once");
        rx.for_each_concurrent(None, |event| self.notify_handlers(event)).await;
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::channel::mpsc;

    struct TestHookFirst {
        callbacks_done: mpsc::UnboundedSender<bool>,
    }

    #[async_trait]
    impl EventHandler<i32> for TestHookFirst {
        async fn on_event(&self, event: i32) {
            assert_eq!(event, 5);
            self.callbacks_done.unbounded_send(true).unwrap();
        }
    }

    struct TestHookSecond {
        callbacks_done: mpsc::UnboundedSender<bool>,
    }

    #[async_trait]
    impl EventHandler<i32> for TestHookSecond {
        async fn on_event(&self, event: i32) {
            assert_eq!(event, 5);
            self.callbacks_done.unbounded_send(true).unwrap();
        }
    }

    #[test]
    fn test_receive_two_handlers() {
        hoist::run(async move {
            let (tx_from_callback, mut rx_from_callback) = mpsc::unbounded::<bool>();
            let (mut queue, processor) = new_queue::<i32>();
            let ((), ()) = futures::join!(
                queue.add_handler(TestHookFirst { callbacks_done: tx_from_callback.clone() }),
                queue.add_handler(TestHookSecond { callbacks_done: tx_from_callback }),
            );
            hoist::spawn(processor.process());
            queue.push(5).await.unwrap();
            assert!(rx_from_callback.next().await.unwrap());
            assert!(rx_from_callback.next().await.unwrap());
        });
    }
}
