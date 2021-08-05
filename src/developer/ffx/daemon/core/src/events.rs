// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Result},
    async_lock::Mutex,
    async_trait::async_trait,
    ffx_core::TryStreamUtilExt,
    fuchsia_async::Task,
    futures::prelude::*,
    pin_project::pin_project,
    std::cmp::Eq,
    std::fmt::Debug,
    std::future::Future,
    std::hash::Hash,
    std::pin::Pin,
    std::rc::{Rc, Weak},
    std::task::{Context, Poll},
    std::time::Duration,
    timeout::timeout,
};

pub trait EventTrait: Debug + Sized + Hash + Clone + Eq {}
impl<T> EventTrait for T where T: Debug + Sized + Hash + Clone + Eq {}

/// An EventSynthesizer is any object that can convert a snapshot of its current
/// state into a vector of events.
#[async_trait(?Send)]
pub trait EventSynthesizer<T: EventTrait> {
    async fn synthesize_events(&self) -> Vec<T>;
}

/// Convenience implementation: if attempting to synthesize events from a weak
/// pointer, returns empty when the weak pointer is no longer valid.
///
/// Leaves a log for debugging.
#[async_trait(?Send)]
impl<T: EventTrait> EventSynthesizer<T> for Weak<dyn EventSynthesizer<T>> {
    async fn synthesize_events(&self) -> Vec<T> {
        let this = match self.upgrade() {
            Some(t) => t,
            None => {
                log::info!("event synthesizer parent Rc<_> lost");
                return Vec::new();
            }
        };
        this.synthesize_events().await
    }
}

/// Determines the status of a handler.
#[derive(PartialEq, Eq, Debug)]
pub enum Status {
    /// Returned when an event handler is done.
    Done,
    /// Returned when an event handler is expecting to handle more events.
    Waiting,
}

/// Implements a general event handler for any inbound events.
#[async_trait(?Send)]
pub trait EventHandler<T: EventTrait> {
    async fn on_event(&self, event: T) -> Result<Status>;
}

struct DispatcherInner<T: EventTrait + 'static> {
    handler: Box<dyn EventHandler<T>>,
    event_in: async_channel::Sender<T>,
}

/// Dispatcher runs events in the handler's queue until the handler is finished,
/// at which point processing ends.
struct Dispatcher<T: EventTrait + 'static> {
    inner: Weak<DispatcherInner<T>>,
    _task: Task<()>,
}

impl<T: EventTrait + 'static> Dispatcher<T> {
    async fn handler_helper(event: T, inner: Rc<DispatcherInner<T>>) -> Result<()> {
        inner
            .handler
            .on_event(event)
            .map(|r| {
                // This block merits some explaining:
                // So originally an event handler would say that it is done by
                // returning Ok(Done). This works around it so that a success
                // result of `Done` will cause the work stream for this handler
                // to close.
                //
                // Otherwise when there is an error, just rewrap it in an Err.
                // This is just a complicated way to remap Result<Status> to
                // Result<()> to preserve the original intended behavior.
                if let Ok(r) = r {
                    if r == Status::Done {
                        Err(anyhow!("dispatcher done"))
                    } else {
                        Ok(())
                    }
                } else {
                    Err(r.unwrap_err())
                }
            })
            .await
    }

    fn new(handler: impl EventHandler<T> + 'static) -> Self {
        let (event_in, queue) = async_channel::unbounded::<T>();
        let inner = Rc::new(DispatcherInner { handler: Box::new(handler), event_in });
        Self {
            inner: Rc::downgrade(&inner),
            _task: Task::local(async move {
                queue
                    .map(|e| Ok(e))
                    .try_for_each_concurrent_while_connected(None, move |e| {
                        Self::handler_helper(e, inner.clone())
                    })
                    .await
                    .unwrap_or_else(|e| log::warn!("dispatcher failed in detached task: {:#?}", e));
            }),
        }
    }

    async fn push(&self, e: T) -> Result<()> {
        let inner = match self.inner.upgrade() {
            Some(i) => i,
            None => return Err(anyhow!("done")),
        };

        inner.event_in.send(e).await.map_err(|e| anyhow!("error enqueueing event: {:#}", e))
    }
}

#[pin_project]
struct PredicateHandlerFuture<F: Future<Output = Result<()>>> {
    // Hack to track whether this future has been dropped, so that eventually
    // the dispatcher will clean the handler up later.
    _inner: Rc<()>,
    #[pin]
    fut: F,
}

impl<F: Future<Output = Result<()>>> PredicateHandlerFuture<F> {
    fn new(inner: Rc<()>, fut: F) -> Self {
        Self { _inner: inner, fut }
    }
}

impl<F: Future<Output = Result<()>>> Future for PredicateHandlerFuture<F> {
    type Output = F::Output;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.project().fut.poll(cx)
    }
}

struct PredicateHandler<T: EventTrait, F>
where
    F: Future<Output = bool>,
{
    parent_link: Weak<()>,
    predicate_matched: async_channel::Sender<()>,
    predicate: Box<dyn Fn(T) -> F + 'static>,
}

impl<T: EventTrait, F> PredicateHandler<T, F>
where
    F: Future<Output = bool>,
{
    fn new(
        parent_link: Weak<()>,
        predicate: impl (Fn(T) -> F) + 'static,
    ) -> (Self, async_channel::Receiver<()>) {
        let (tx, rx) = async_channel::unbounded::<()>();
        let s = Self { parent_link, predicate_matched: tx, predicate: Box::new(predicate) };

        (s, rx)
    }
}

#[async_trait(?Send)]
impl<T, F> EventHandler<T> for PredicateHandler<T, F>
where
    T: EventTrait,
    F: Future<Output = bool>,
{
    async fn on_event(&self, event: T) -> Result<Status> {
        // This is a bit of a race, but will eventually clean things up by the
        // time the next event fires (if the wait_for future is dropped).
        if self.parent_link.upgrade().is_none() {
            return Ok(Status::Done);
        }
        if (self.predicate)(event).await {
            self.predicate_matched.send(()).await.context("sending 'done' signal to waiter")?;
            return Ok(Status::Done);
        }
        Ok(Status::Waiting)
    }
}

type Handlers<T> = Rc<Mutex<Vec<Dispatcher<T>>>>;

#[derive(Clone)]
pub struct Queue<T: EventTrait + 'static> {
    inner_tx: async_channel::Sender<T>,
    handlers: Handlers<T>,
    state: Weak<dyn EventSynthesizer<T>>,

    // Rc<_> so that the client can drop multiple of these clients without
    // having the underlying task dropped/canceled.
    _processor_task: Rc<Task<()>>,
}

struct Processor<T: 'static + EventTrait> {
    inner_rx: Option<async_channel::Receiver<T>>,
    handlers: Handlers<T>,
}

impl<T: 'static + EventTrait> Queue<T> {
    /// Creates an event queue. The state is tracked with a `Weak<_>` pointer to
    /// `state`.
    ///
    /// When this is called, an event processing task is started in the
    /// background and tied to the lifetimes of these objects. Once all objects
    /// are dropped, the background process will be shutdown automatically.
    pub fn new(state: &Rc<impl EventSynthesizer<T> + 'static>) -> Self {
        let (inner_tx, inner_rx) = async_channel::unbounded::<T>();
        let handlers = Rc::new(Mutex::new(Vec::<Dispatcher<T>>::new()));
        let proc = Processor::<T> { inner_rx: Some(inner_rx), handlers: handlers.clone() };
        let state = Rc::downgrade(state);
        Self { inner_tx, handlers, state, _processor_task: Rc::new(Task::local(proc.process())) }
    }

    /// Creates an event queue (see `new`) with a single handler to start.
    #[allow(unused)] // TODO(awdavies): This will be needed later for target events.
    pub fn new_with_handler(
        state: &Rc<impl EventSynthesizer<T> + 'static>,
        handler: impl EventHandler<T> + 'static,
    ) -> Self {
        let (inner_tx, inner_rx) = async_channel::unbounded::<T>();
        let handlers = Rc::new(Mutex::new(vec![Dispatcher::new(handler)]));
        let proc = Processor::<T> { inner_rx: Some(inner_rx), handlers: handlers.clone() };
        let state = Rc::downgrade(state);
        Self { inner_tx, handlers, state, _processor_task: Rc::new(Task::local(proc.process())) }
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
            // If an error occurs in the event handler its Rc<_> will be dropped,
            // so just return if there's an error. The result for continuing and
            // adding the dispatcher anyway would be about the same, this just
            // makes cleanup slightly faster.
            match dispatcher
                .push(event.clone())
                .await
                .context("sending synthesized event to child queue")
            {
                Ok(_) => (),
                Err(e) => {
                    log::warn!("{}", e);
                    return;
                }
            }
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
        predicate: impl Fn(T) -> bool + 'static,
    ) -> Result<()> {
        self.wait_for_async(timeout, move |e| future::ready(predicate(e))).await
    }

    /// The async version of `wait_for` (See: `wait_for`).
    pub async fn wait_for_async<F1>(
        &self,
        timeout_opt: Option<Duration>,
        predicate: impl Fn(T) -> F1 + 'static,
    ) -> Result<()>
    where
        F1: Future<Output = bool> + 'static,
    {
        let link = Rc::new(());
        let parent_link = Rc::downgrade(&link);
        let (handler, mut handler_done) = PredicateHandler::new(parent_link, move |t| predicate(t));
        let fut = async move {
            handler_done
                .next()
                .await
                .unwrap_or_else(|| log::warn!("unable to get 'done' signal from handler."));
            Result::<()>::Ok(())
        };
        self.add_handler(handler).await;
        if let Some(t) = timeout_opt {
            PredicateHandlerFuture::new(link, async move {
                timeout(t, fut).await.map_err(|e| anyhow!("waiting for event: {:#}", e))?
            })
            .await
        } else {
            PredicateHandlerFuture::new(link, fut).await
        }
    }

    pub fn push(&self, event: T) -> Result<()> {
        self.inner_tx.try_send(event).map_err(|e| anyhow!("event queue push: {:#}", e))
    }
}

impl<T> Processor<T>
where
    T: EventTrait + 'static,
{
    async fn dispatch(&self, event: T) {
        let mut handlers = self.handlers.lock().await;

        let mut new_handlers = Vec::new();
        for dispatcher in handlers.drain(..) {
            match dispatcher.push(event.clone()).await {
                Ok(()) => new_handlers.push(dispatcher),
                Err(e) => {
                    log::info!("dispatcher closed. reason: {:#}", e);
                }
            }
        }
        *handlers = new_handlers;
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

    struct TestHookFirst {
        callbacks_done: async_channel::Sender<bool>,
    }

    #[async_trait(?Send)]
    impl EventHandler<i32> for TestHookFirst {
        async fn on_event(&self, event: i32) -> Result<Status> {
            assert_eq!(event, 5);
            self.callbacks_done.send(true).await.unwrap();
            Ok(Status::Done)
        }
    }

    struct TestHookSecond {
        callbacks_done: async_channel::Sender<bool>,
    }

    #[async_trait(?Send)]
    impl EventHandler<i32> for TestHookSecond {
        async fn on_event(&self, event: i32) -> Result<Status> {
            assert_eq!(event, 5);
            self.callbacks_done.send(true).await.unwrap();
            Ok(Status::Waiting)
        }
    }

    struct FakeEventStruct {}

    #[async_trait(?Send)]
    impl<T: EventTrait + 'static> EventSynthesizer<T> for FakeEventStruct {
        async fn synthesize_events(&self) -> Vec<T> {
            vec![]
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_receive_two_handlers() {
        let (tx_from_callback, mut rx_from_callback) = async_channel::unbounded::<bool>();
        let fake_events = Rc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let ((), ()) = futures::join!(
            queue.add_handler(TestHookFirst { callbacks_done: tx_from_callback.clone() }),
            queue.add_handler(TestHookSecond { callbacks_done: tx_from_callback }),
        );
        queue.push(5).unwrap();
        assert!(rx_from_callback.next().await.unwrap());
        assert!(rx_from_callback.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_wait_for_event_once_async() {
        let fake_events = Rc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        queue.push(5).unwrap();
        queue
            .wait_for_async(None, |e| async move {
                assert_eq!(e, 5);
                true
            })
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_wait_for_event_once() {
        let fake_events = Rc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        queue.push(5).unwrap();
        queue.wait_for(None, |e| e == 5).await.unwrap();
    }

    struct FakeEventSynthesizer {}

    #[async_trait(?Send)]
    impl EventSynthesizer<i32> for FakeEventSynthesizer {
        async fn synthesize_events(&self) -> Vec<i32> {
            vec![2, 3, 7, 6]
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_wait_for_event_synthetic() {
        let fake_events = Rc::new(FakeEventSynthesizer {});
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
        let fake_events = Rc::new(FakeEventSynthesizer {});
        let weak = Rc::downgrade(&fake_events);
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
        dropped: async_channel::Sender<bool>,
    }

    impl EventFailer {
        fn new() -> (Self, async_channel::Receiver<bool>) {
            let (dropped, handler_dropped_rx) = async_channel::unbounded::<bool>();
            (Self { dropped }, handler_dropped_rx)
        }
    }

    impl Drop for EventFailer {
        fn drop(&mut self) {
            // TODO(raggi): use a safer executor
            futures::executor::block_on(self.dropped.send(true)).unwrap();
        }
    }

    #[async_trait(?Send)]
    impl EventHandler<EventFailerInput> for EventFailer {
        async fn on_event(&self, event: EventFailerInput) -> Result<Status> {
            match event {
                EventFailerInput::Fail => Err(anyhow!("test told to fail")),
                EventFailerInput::Complete => Ok(Status::Done),
            }
        }
    }

    struct EventFailerState {}

    #[async_trait(?Send)]
    impl EventSynthesizer<EventFailerInput> for EventFailerState {
        async fn synthesize_events(&self) -> Vec<EventFailerInput> {
            vec![EventFailerInput::Fail]
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_failure_drops_handler_synth_events() {
        let fake_events = Rc::new(EventFailerState {});
        let queue = Queue::new(&fake_events);
        let (handler, mut handler_dropped_rx) = EventFailer::new();
        queue.add_handler(handler).await;
        assert!(handler_dropped_rx.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_failure_drops_handler() {
        let fake_events = Rc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let (handler, mut handler_dropped_rx) = EventFailer::new();
        let (handler2, mut handler_dropped_rx2) = EventFailer::new();
        let ((), ()) = futures::join!(queue.add_handler(handler), queue.add_handler(handler2));
        queue.push(EventFailerInput::Fail).unwrap();
        assert!(handler_dropped_rx.next().await.unwrap());
        assert!(handler_dropped_rx2.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_done_drops_handler() {
        let fake_events = Rc::new(FakeEventStruct {});
        let queue = Queue::new(&fake_events);
        let (handler, mut handler_dropped_rx) = EventFailer::new();
        let (handler2, mut handler_dropped_rx2) = EventFailer::new();
        let ((), ()) = futures::join!(queue.add_handler(handler), queue.add_handler(handler2));
        queue.push(EventFailerInput::Complete).unwrap();
        assert!(handler_dropped_rx.next().await.unwrap());
        assert!(handler_dropped_rx2.next().await.unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_wait_for_timeout() {
        let fake_events = Rc::new(FakeEventStruct {});
        let queue = Queue::<i32>::new(&fake_events);
        assert!(queue.wait_for(Some(Duration::from_millis(1)), |_| true).await.is_err());
    }
}
