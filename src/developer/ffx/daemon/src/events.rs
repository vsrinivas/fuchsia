// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::ok_or_return,
    crate::target,
    anyhow::{anyhow, Context, Result},
    async_std::future::timeout,
    async_trait::async_trait,
    fuchsia_async::Task,
    futures::channel::mpsc,
    futures::lock::Mutex,
    futures::prelude::*,
    std::cmp::Eq,
    std::default::Default,
    std::fmt::Debug,
    std::hash::Hash,
    std::net::SocketAddr,
    std::sync::{Arc, Weak},
    std::time::Duration,
};

pub trait EventTrait: Debug + Sized + Hash + Clone + Eq + Send + Sync {}
impl<T> EventTrait for T where T: Debug + Sized + Hash + Clone + Eq + Send + Sync {}

/// An EventSynthesizer is any object that can convert a snapshot of its current
/// state into a vector of events.
#[async_trait]
pub trait EventSynthesizer<T: EventTrait>: Send + Sync {
    async fn synthesize_events(&self) -> Vec<T>;
}

/// Convenience implementation: if attempting to synthesize events from a weak
/// pointer, returns empty when the weak pointer is no longer valid.
///
/// Leaves a log for debugging.
#[async_trait]
impl<T: EventTrait> EventSynthesizer<T> for Weak<dyn EventSynthesizer<T>> {
    async fn synthesize_events(&self) -> Vec<T> {
        let this = match self.upgrade() {
            Some(t) => t,
            None => {
                log::info!("event synthesizer parent Arc<_> lost");
                return Vec::new();
            }
        };
        this.synthesize_events().await
    }
}

pub trait TryIntoTargetInfo: Sized {
    type Error;

    /// Attempts, given a source socket address, to determine whether the
    /// received message was from a Fuchsia target, and if so, what kind. Attempts
    /// to fill in as much information as possible given the message, consuming
    /// the underlying object in the process.
    fn try_into_target_info(self, src: SocketAddr) -> Result<TargetInfo, Self::Error>;
}

/// Implements a general event handler for any inbound events.
#[async_trait]
pub trait EventHandler<T: EventTrait>: Send + Sync {
    async fn on_event(&self, event: T) -> Result<bool>;
}

#[derive(Debug, Default, Hash, Clone, PartialEq, Eq)]
pub struct TargetInfo {
    pub nodename: String,
    pub addresses: Vec<target::TargetAddr>,
    pub serial: Option<String>,
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
    NewTarget(String),
    // TODO(awdavies): Stale target event, target shutdown event, etc.
}

struct DispatcherInner<T: EventTrait + 'static> {
    handler: Box<dyn EventHandler<T>>,
    event_in: mpsc::UnboundedSender<T>,
}

/// Dispatcher runs events in the handler's queue until the handler is finished,
/// at which point processing ends.
struct Dispatcher<T: EventTrait + 'static> {
    inner: Weak<DispatcherInner<T>>,
    _task: Task<()>,
}

impl<T: EventTrait + 'static> Dispatcher<T> {
    fn new(handler: impl EventHandler<T> + 'static) -> Self {
        let (event_in, mut queue) = mpsc::unbounded::<T>();
        let inner = Arc::new(DispatcherInner { handler: Box::new(handler), event_in });

        Self {
            inner: Arc::downgrade(&inner),

            _task: Task::spawn(async move {
                // All events should be handled serially. try_for_each didn't appear to
                // be implemented for UnboundedReceiver<T>.
                while let Some(e) = queue.next().await {
                    if inner.handler.on_event(e).await.unwrap_or_else(|e| {
                        log::warn!("event handler failed, exiting task: {:#}", e);
                        true // "it is true we're done."
                    }) {
                        break;
                    }
                }
            }),
        }
    }

    fn push(&self, e: T) -> Result<()> {
        let inner = match self.inner.upgrade() {
            Some(i) => i,
            None => return Err(anyhow!("done")),
        };

        inner.event_in.unbounded_send(e).map_err(|e| anyhow!("error enqueueing event: {:#}", e))
    }
}

struct PredicateHandler<T: EventTrait, F>
where
    F: Future<Output = bool> + Send + Sync,
{
    predicate_matched: mpsc::UnboundedSender<()>,
    predicate: Box<dyn Fn(T) -> F + Send + Sync + 'static>,
}

impl<T: EventTrait, F> PredicateHandler<T, F>
where
    F: Future<Output = bool> + Send + Sync,
{
    fn new(
        predicate: impl (Fn(T) -> F) + Send + Sync + 'static,
    ) -> (Self, mpsc::UnboundedReceiver<()>) {
        let (tx, rx) = mpsc::unbounded::<()>();
        let s = Self { predicate_matched: tx, predicate: Box::new(predicate) };

        (s, rx)
    }
}

#[async_trait]
impl<T, F> EventHandler<T> for PredicateHandler<T, F>
where
    T: EventTrait,
    F: Future<Output = bool> + Send + Sync,
{
    async fn on_event(&self, event: T) -> Result<bool> {
        if (self.predicate)(event).await {
            self.predicate_matched.unbounded_send(()).context("sending 'done' signal to waiter")?;
            return Ok(true);
        }
        Ok(false)
    }
}

type Handlers<T> = Arc<Mutex<Vec<Dispatcher<T>>>>;

#[derive(Clone)]
pub struct Queue<T: EventTrait + 'static> {
    inner_tx: mpsc::UnboundedSender<T>,
    handlers: Handlers<T>,
    state: Weak<dyn EventSynthesizer<T>>,

    // Arc<_> so that the client can drop multiple of these clients without
    // having the underlying task dropped/canceled.
    _processor_task: Arc<Task<()>>,
}

struct Processor<T: 'static + EventTrait> {
    inner_rx: Option<mpsc::UnboundedReceiver<T>>,
    handlers: Handlers<T>,
}

impl<T: 'static + EventTrait> Queue<T> {
    /// Creates an event queue. The state is tracked with a `Weak<_>` pointer to
    /// `state`.
    ///
    /// When this is called, an event processing task is started in the
    /// background and tied to the lifetimes of these objects. Once all objects
    /// are dropped, the background process will be shutdown automatically.
    pub fn new(state: &Arc<impl EventSynthesizer<T> + 'static>) -> Self {
        let (inner_tx, inner_rx) = mpsc::unbounded::<T>();
        let handlers = Arc::new(Mutex::new(Vec::<Dispatcher<T>>::new()));
        let proc = Processor::<T> { inner_rx: Some(inner_rx), handlers: handlers.clone() };
        let state = Arc::downgrade(state);
        Self { inner_tx, handlers, state, _processor_task: Arc::new(Task::spawn(proc.process())) }
    }

    /// Creates an event queue (see `new`) with a single handler to start.
    #[allow(unused)] // TODO(awdavies): This will be needed later for target events.
    pub fn new_with_handler(
        state: &Arc<impl EventSynthesizer<T> + 'static>,
        handler: impl EventHandler<T> + 'static,
    ) -> Self {
        let (inner_tx, inner_rx) = mpsc::unbounded::<T>();
        let handlers = Arc::new(Mutex::new(vec![Dispatcher::new(handler)]));
        let proc = Processor::<T> { inner_rx: Some(inner_rx), handlers: handlers.clone() };
        let state = Arc::downgrade(state);
        Self { inner_tx, handlers, state, _processor_task: Arc::new(Task::spawn(proc.process())) }
    }

    /// Adds an event handler, which is fired every time an event comes in.
    /// Before this happens, though, the event dispatcher associated with this
    /// `EventHandler<_>` will send a list of synthesized events to the handler
    /// derived from the internal state.
    pub async fn add_handler(&self, handler: impl EventHandler<T> + 'static) {
        // Locks the handlers so that they cannot receive events, then obtains
        // the state as it will (hopefully) not have had any updates after
        // acquiring the lock, on account of it not being able to push new
        // events to the queue.
        let mut handlers = self.handlers.lock().await;
        let synth_events = self.state.synthesize_events().await;
        let dispatcher = Dispatcher::new(handler);
        for event in synth_events.iter() {
            // If an error occurs in the event handler its Arc<_> will be dropped,
            // so just return if there's an error. The result for continuing and
            // adding the dispatcher anyway would be about the same, this just
            // makes cleanup slightly faster.
            ok_or_return!(dispatcher
                .push(event.clone())
                .context("failed to send synthesized event to child queue"));
        }
        handlers.push(dispatcher);
    }

    /// Waits for an event to occur. An event has occured when the closure
    /// passed to this function evaluates to `true`.
    ///
    /// If timeout is `None`, this will run forever, else this will return an
    /// `Error` if the timeout is reached (`Error` will only ever be returned
    /// for a timeout).
    pub async fn wait_for(
        &self,
        timeout: Option<Duration>,
        predicate: impl Fn(T) -> bool + Send + Sync + 'static,
    ) -> Result<()> {
        self.wait_for_async(timeout, move |e| future::ready(predicate(e))).await
    }

    /// The async version of `wait_for` (See: `wait_for`).
    pub async fn wait_for_async<F>(
        &self,
        timeout_opt: Option<Duration>,
        predicate: impl Fn(T) -> F + Send + Sync + 'static,
    ) -> Result<()>
    where
        F: Future<Output = bool> + Send + Sync + 'static,
    {
        let (handler, mut handler_done) = PredicateHandler::new(move |t| predicate(t));
        self.add_handler(handler).await;
        let fut = async move {
            handler_done
                .next()
                .await
                .unwrap_or_else(|| log::warn!("unable to get 'done' signal from handler."))
        };

        match timeout_opt {
            None => Ok(fut.await),
            Some(t) => timeout(t, fut).await.map_err(|e| anyhow!("waiting for event: {:#}", e)),
        }
    }

    pub async fn push(&self, event: T) -> Result<()> {
        self.inner_tx.unbounded_send(event).context("enqueueing")
    }
}

impl<T> Processor<T>
where
    T: EventTrait + 'static,
{
    async fn dispatch(&self, event: T) {
        self.handlers.lock().await.retain(|dispatcher| match dispatcher.push(event.clone()) {
            Ok(()) => true,
            Err(e) => {
                log::info!("dispatcher closed. reason: {:#}", e);
                false
            }
        });
    }

    /// Consumes the processor and then runs until all instances of the Queue are closed.
    async fn process(mut self) {
        if let Some(rx) = self.inner_rx.take() {
            rx.for_each(|event| self.dispatch(event)).await;
        } else {
            log::warn!("process should only ever be called once");
        }
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
        async fn on_event(&self, event: i32) -> Result<bool> {
            assert_eq!(event, 5);
            self.callbacks_done.unbounded_send(true).unwrap();
            Ok(false)
        }
    }

    struct TestHookSecond {
        callbacks_done: mpsc::UnboundedSender<bool>,
    }

    #[async_trait]
    impl EventHandler<i32> for TestHookSecond {
        async fn on_event(&self, event: i32) -> Result<bool> {
            assert_eq!(event, 5);
            self.callbacks_done.unbounded_send(true).unwrap();
            Ok(false)
        }
    }

    struct FakeEventStruct {}

    #[async_trait]
    impl<T: EventTrait + 'static> EventSynthesizer<T> for FakeEventStruct {
        async fn synthesize_events(&self) -> Vec<T> {
            vec![]
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_receive_two_handlers() {
        let (tx_from_callback, mut rx_from_callback) = mpsc::unbounded::<bool>();
        let fake_events = Arc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let ((), ()) = futures::join!(
            queue.add_handler(TestHookFirst { callbacks_done: tx_from_callback.clone() }),
            queue.add_handler(TestHookSecond { callbacks_done: tx_from_callback }),
        );
        queue.push(5).await.unwrap();
        assert!(rx_from_callback.next().await.unwrap());
        assert!(rx_from_callback.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_wait_for_event_once_async() {
        let fake_events = Arc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let (res1, res2) = futures::join!(
            queue.wait_for_async(None, |e| async move {
                assert_eq!(e, 5);
                true
            }),
            queue.push(5)
        );
        res1.unwrap();
        res2.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_wait_for_event_once() {
        let fake_events = Arc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let (res1, res2) = futures::join!(queue.wait_for(None, |e| e == 5), queue.push(5),);
        res1.unwrap();
        res2.unwrap();
    }

    struct FakeEventSynthesizer {}

    #[async_trait]
    impl EventSynthesizer<i32> for FakeEventSynthesizer {
        async fn synthesize_events(&self) -> Vec<i32> {
            vec![2, 3, 7, 6]
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_wait_for_event_synthetic() {
        let fake_events = Arc::new(FakeEventSynthesizer {});
        let queue = Queue::new(&fake_events);
        let (one, two, three, four) = futures::join!(
            queue.wait_for(None, |e| e == 7),
            queue.wait_for(None, |e| e == 6),
            queue.wait_for(None, |e| e == 2),
            queue.wait_for(None, |e| e == 3)
        );
        one.unwrap();
        two.unwrap();
        three.unwrap();
        four.unwrap();
    }

    // This is mostly here to fool the compiler, as for whatever reason invoking
    // `synthesize_events()` directly on a `Weak<_>` doesn't work.
    async fn test_event_synth_func<T: EventTrait>(es: Weak<dyn EventSynthesizer<T>>) -> Vec<T> {
        es.synthesize_events().await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_synthesis_dropped_state() {
        let fake_events = Arc::new(FakeEventSynthesizer {});
        let weak = Arc::downgrade(&fake_events);
        std::mem::drop(fake_events);
        let vec = test_event_synth_func(weak).await;
        assert_eq!(vec.len(), 0);
    }

    #[derive(Debug, Hash, Clone, PartialEq, Eq)]
    enum EventFailerInput {
        Fail,
        Complete,
    }

    struct EventFailer {
        dropped: mpsc::UnboundedSender<bool>,
    }

    impl EventFailer {
        fn new() -> (Self, mpsc::UnboundedReceiver<bool>) {
            let (dropped, handler_dropped_rx) = mpsc::unbounded::<bool>();
            (Self { dropped }, handler_dropped_rx)
        }
    }

    impl Drop for EventFailer {
        fn drop(&mut self) {
            self.dropped.unbounded_send(true).unwrap();
        }
    }

    #[async_trait]
    impl EventHandler<EventFailerInput> for EventFailer {
        async fn on_event(&self, event: EventFailerInput) -> Result<bool> {
            match event {
                EventFailerInput::Fail => Err(anyhow!("test told to fail")),
                EventFailerInput::Complete => Ok(true),
            }
        }
    }

    struct EventFailerState {}

    #[async_trait]
    impl EventSynthesizer<EventFailerInput> for EventFailerState {
        async fn synthesize_events(&self) -> Vec<EventFailerInput> {
            vec![EventFailerInput::Fail]
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_failure_drops_handler_synth_events() {
        let fake_events = Arc::new(EventFailerState {});
        let queue = Queue::new(&fake_events);
        let (handler, mut handler_dropped_rx) = EventFailer::new();
        queue.add_handler(handler).await;
        assert!(handler_dropped_rx.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_failure_drops_handler() {
        let fake_events = Arc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let (handler, mut handler_dropped_rx) = EventFailer::new();
        let (handler2, mut handler_dropped_rx2) = EventFailer::new();
        let ((), ()) = futures::join!(queue.add_handler(handler), queue.add_handler(handler2));
        queue.push(EventFailerInput::Fail).await.unwrap();
        assert!(handler_dropped_rx.next().await.unwrap());
        assert!(handler_dropped_rx2.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_done_drops_handler() {
        let fake_events = Arc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let (handler, mut handler_dropped_rx) = EventFailer::new();
        let (handler2, mut handler_dropped_rx2) = EventFailer::new();
        let ((), ()) = futures::join!(queue.add_handler(handler), queue.add_handler(handler2));
        queue.push(EventFailerInput::Complete).await.unwrap();
        assert!(handler_dropped_rx.next().await.unwrap());
        assert!(handler_dropped_rx2.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_wait_for_timeout() {
        let fake_events = Arc::new(FakeEventStruct {});
        let queue = Queue::<i32>::new(&fake_events);
        assert!(queue.wait_for(Some(Duration::from_millis(1)), |_| true).await.is_err());
    }
}
