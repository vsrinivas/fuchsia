// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_testing::{
    FakeClockControlRequest, FakeClockControlRequestStream, FakeClockRequest,
    FakeClockRequestStream, Increment,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::{self as zx, AsHandleRef, DurationNum, Peered};
use futures::{channel::oneshot, stream::StreamExt, FutureExt};
use log::{debug, warn};

use std::collections::{BinaryHeap, HashMap};
use std::convert::TryFrom;
use std::sync::{Arc, Mutex};

const DEFAULT_INCREMENTS_MS: i64 = 10;

#[derive(Debug)]
struct PendingEvent<E = zx::Koid> {
    time: zx::Time,
    event: E,
}

struct RegisteredEvent {
    event: zx::EventPair,
    pending: bool,
}

impl<E> Eq for PendingEvent<E> {}
impl<E> PartialEq for PendingEvent<E> {
    fn eq(&self, other: &Self) -> bool {
        self.time == other.time
    }
}

impl<E> PartialOrd for PendingEvent<E> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl<E> Ord for PendingEvent<E> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        other.time.cmp(&self.time)
    }
}

impl RegisteredEvent {
    fn signal(&mut self) {
        self.pending = false;
        if let Err(e) = self.event.signal_peer(zx::Signals::NONE, zx::Signals::EVENT_SIGNALED) {
            debug!("Failed to signal event: {:?}", e);
        } else {
            debug!("Signaled event");
        }
    }

    fn clear(&mut self) {
        self.pending = false;
        if let Err(e) = self.event.signal_peer(zx::Signals::EVENT_SIGNALED, zx::Signals::NONE) {
            debug!("Failed to clear event: {:?}", e);
        }
    }
}

/// The fake clock implementation.
/// Type parameter `T` is used to observe events during testing.
/// The empty tuple `()` implements `FakeClockObserver` and is meant to be used
/// for production instances.
struct FakeClock<T> {
    time: zx::Time,
    free_running: Option<(oneshot::Sender<()>, oneshot::Receiver<()>)>,
    pending_events: BinaryHeap<PendingEvent>,
    registered_events: HashMap<zx::Koid, RegisteredEvent>,
    observer: T,
}

trait FakeClockObserver: 'static {
    fn new() -> Self;
    fn event_removed(&mut self, koid: zx::Koid);
}

impl FakeClockObserver for () {
    fn new() -> () {
        ()
    }
    fn event_removed(&mut self, _koid: zx::Koid) {
        /* do nothing, the trait is just used for testing */
    }
}

impl<T: FakeClockObserver> FakeClock<T> {
    fn new() -> Self {
        FakeClock {
            time: zx::Time::from_nanos(1),
            free_running: None,
            pending_events: BinaryHeap::new(),
            registered_events: HashMap::new(),
            observer: T::new(),
        }
    }

    fn is_free_running(&self) -> bool {
        self.free_running.is_some()
    }

    fn check_events(&mut self) {
        while let Some(e) = self.pending_events.peek() {
            if e.time <= self.time {
                let koid = self.pending_events.pop().unwrap().event;
                self.registered_events.get_mut(&koid).unwrap().signal();
            } else {
                debug!("Next event in {:?}", e.time - self.time);
                break;
            }
        }
    }

    fn install_event(
        &mut self,
        arc_self: FakeClockHandle<T>,
        time: zx::Time,
        event: zx::EventPair,
    ) {
        let koid = if let Ok(koid) = event.basic_info().map(|i| i.related_koid) {
            koid
        } else {
            return;
        };
        // avoid installing duplicate events if user is calling the API by
        // mistake, but warn in log.
        if self.registered_events.contains_key(&koid) {
            warn!("RegisterEvent called with already known event, rescheduling instead.");
            self.reschedule_event(time, koid);
            return;
        }

        let closed_fut = fasync::OnSignals::new(&event, zx::Signals::EVENTPAIR_CLOSED)
            .extend_lifetime()
            .map(move |_| {
                let mut mc = arc_self.lock().unwrap();
                mc.cancel_event(koid);
                mc.registered_events.remove(&koid).expect("Registered event disappeared");
                mc.observer.event_removed(koid);
            });

        let pending = PendingEvent { time, event: koid };
        let mut registered = RegisteredEvent { pending: pending.time > self.time, event };

        if registered.pending {
            debug!("Registering event at {:?} -> {:?}", time, time - self.time);
            self.pending_events.push(pending);
        } else {
            // signal immediately if the deadline is in the past.
            registered.signal();
        };

        self.registered_events.insert(koid, registered);
        fasync::spawn_local(closed_fut);
    }

    fn reschedule_event(&mut self, time: zx::Time, koid: zx::Koid) {
        // always cancel the event if pending.
        self.cancel_event(koid);
        let entry = if let Some(e) = self.registered_events.get_mut(&koid) {
            e
        } else {
            warn!("Unrecognized event in reschedule call");
            return;
        };
        if time <= self.time {
            debug!("Immediately signaling reschedule to {:?}", time);
            entry.signal();
        } else {
            debug!("Rescheduling event at {:?} -> {:?}", time, time - self.time);
            entry.pending = true;
            self.pending_events.push(PendingEvent { time, event: koid });
        }
    }

    fn cancel_event(&mut self, koid: zx::Koid) {
        let entry = if let Some(e) = self.registered_events.get_mut(&koid) {
            e
        } else {
            warn!("Unrecognized event in cancel call");
            return;
        };
        if entry.pending {
            self.pending_events = self
                .pending_events
                .drain()
                .filter(|e| {
                    if e.event != koid {
                        true
                    } else {
                        // clear any signals in the event if we're cancelling it
                        debug!("Cancelling event registered at {:?}", e.time);
                        false
                    }
                })
                .collect::<Vec<_>>()
                .into();
        }
        // always clear signals (even if entry was not pending)
        entry.clear();
    }

    fn increment(&mut self, increment: &Increment) {
        let dur = match increment {
            Increment::Determined(d) => *d,
            Increment::Random(rr) => {
                if let Ok(v) = u64::try_from(rr.min_rand).and_then(|min| {
                    u64::try_from(rr.max_rand)
                        .map(|max| min + (rand::random::<u64>() % (max - min)))
                        .and_then(i64::try_from)
                }) {
                    v
                } else {
                    DEFAULT_INCREMENTS_MS
                }
            }
        }
        .nanos();
        debug!("incrementing mock clock {:?} => {:?}", increment, dur);
        self.time += dur;
        self.check_events();
    }
}

type FakeClockHandle<T> = Arc<Mutex<FakeClock<T>>>;

fn start_free_running<T: FakeClockObserver>(
    mock_clock: &FakeClockHandle<T>,
    real_increment: zx::Duration,
    increment: Increment,
) {
    let (mut signal, finished) = {
        let mut mc = mock_clock.lock().unwrap();
        assert!(mc.free_running.is_none());
        let (sender, signal) = oneshot::channel();
        let (finished, end_signal) = oneshot::channel();
        mc.free_running = Some((sender, end_signal));
        (signal.into_stream(), finished)
    };
    let mock_clock = Arc::clone(mock_clock);
    fasync::spawn_local(async move {
        let mut itv = fasync::Interval::new(real_increment);
        debug!("free running mock clock {:?} {:?}", real_increment, increment);
        loop {
            if futures::select! {
                itv = itv.next() => {
                    mock_clock.lock().unwrap().increment(&increment);
                    false
                },
                signal = signal.next() => {
                    true
                }
            } {
                break;
            }
        }
        finished.send(()).unwrap();
    });
}

async fn stop_free_running<T: FakeClockObserver>(mock_clock: &FakeClockHandle<T>) {
    let mut mc = mock_clock.lock().unwrap();
    if mc.is_free_running() {
        if let Some((s, r)) = mc.free_running.take() {
            s.send(()).unwrap();
            let () = r.into_stream().next().await.unwrap().unwrap();
        }
    }
}

fn check_valid_increment(increment: &Increment) -> bool {
    match increment {
        Increment::Determined(_) => true,
        Increment::Random(rr) => rr.min_rand >= 0 && rr.max_rand >= 0 && rr.max_rand > rr.min_rand,
    }
}

async fn handle_control_events<T: FakeClockObserver>(
    mock_clock: FakeClockHandle<T>,
    mut rs: FakeClockControlRequestStream,
) {
    while let Some(Ok(req)) = rs.next().await {
        match req {
            FakeClockControlRequest::Advance { increment, responder } => {
                if check_valid_increment(&increment) {
                    let mut mc = mock_clock.lock().unwrap();
                    if mc.is_free_running() {
                        let _ = responder.send(&mut Err(zx::Status::ACCESS_DENIED.into_raw()));
                    } else {
                        mc.increment(&increment);
                        let _ = responder.send(&mut Ok(()));
                    }
                } else {
                    let _ = responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()));
                }
            }
            FakeClockControlRequest::Pause { responder } => {
                stop_free_running(&mock_clock).await;
                let _ = responder.send();
            }
            FakeClockControlRequest::ResumeWithIncrements { real, increment, responder } => {
                if real <= 0 || !check_valid_increment(&increment) {
                    let _ = responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()));
                } else {
                    // stop free running if we are
                    stop_free_running(&mock_clock).await;
                    start_free_running(&mock_clock, real.nanos(), increment);
                    let _ = responder.send(&mut Ok(()));
                }
            }
        }
    }
}

async fn handle_events<T: FakeClockObserver>(
    mock_clock: FakeClockHandle<T>,
    mut rs: FakeClockRequestStream,
) {
    while let Some(Ok(req)) = rs.next().await {
        match req {
            FakeClockRequest::RegisterEvent { time, event, control_handle: _ } => {
                mock_clock.lock().unwrap().install_event(
                    Arc::clone(&mock_clock),
                    zx::Time::from_nanos(time),
                    event.into(),
                );
            }
            FakeClockRequest::Get { responder } => {
                let _ = responder.send(mock_clock.lock().unwrap().time.into_nanos());
            }
            FakeClockRequest::RescheduleEvent { event, time, responder } => {
                if let Ok(k) = event.get_koid() {
                    mock_clock.lock().unwrap().reschedule_event(zx::Time::from_nanos(time), k)
                }
                let _ = responder.send();
            }
            FakeClockRequest::CancelEvent { event, responder } => {
                if let Ok(k) = event.get_koid() {
                    mock_clock.lock().unwrap().cancel_event(k);
                }
                let _ = responder.send();
            }
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().expect("failed to initialize logger");
    fuchsia_syslog::set_severity(-2);

    debug!("Starting mock clock service");

    let mock_clock = Arc::new(Mutex::new(FakeClock::<()>::new()));
    start_free_running(
        &mock_clock,
        DEFAULT_INCREMENTS_MS.millis(),
        Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
    );
    let m1 = Arc::clone(&mock_clock);

    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(move |rs: FakeClockControlRequestStream| {
            let cl = Arc::clone(&mock_clock);
            fasync::spawn_local(handle_control_events(cl, rs))
        })
        .add_fidl_service(move |rs: FakeClockRequestStream| {
            let cl = Arc::clone(&m1);
            fasync::spawn_local(handle_events(cl, rs))
        });
    fs.take_and_serve_directory_handle()?;
    let () = fs.collect().await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon::Koid;
    use futures::channel::mpsc;
    use std::sync::Once;

    /// log::Log implementation that uses stdout.
    ///
    /// Useful when debugging tests.
    struct Logger;

    impl log::Log for Logger {
        fn enabled(&self, _metadata: &log::Metadata) -> bool {
            true
        }

        fn log(&self, record: &log::Record) {
            println!("{}", record.args())
        }

        fn flush(&self) {}
    }

    static LOGGER: Logger = Logger;

    static LOGGER_ONCE: Once = Once::new();

    fn set_logger_for_test() {
        // log::set_logger will panic if called multiple times; using a Once makes
        // set_logger_for_test idempotent
        LOGGER_ONCE.call_once(|| {
            log::set_logger(&LOGGER).unwrap();
            log::set_max_level(log::LevelFilter::Trace);
        })
    }

    #[test]
    fn test_event_heap() {
        set_logger_for_test();
        let time = zx::Time::get(zx::ClockId::Monotonic);
        let after = time + 10.millis();
        let e1 = PendingEvent { time, event: 0 };
        let e2 = PendingEvent { time: after, event: 1 };
        let mut heap = BinaryHeap::new();
        heap.push(e2);
        heap.push(e1);
        assert_eq!(heap.pop().unwrap().time, time);
        assert_eq!(heap.pop().unwrap().time, after);
    }

    #[test]
    fn test_simple_increments() {
        set_logger_for_test();
        let mut mock_clock = FakeClock::<()>::new();
        let begin = mock_clock.time;
        let skip = 10.millis();
        mock_clock.increment(&Increment::Determined(skip.into_nanos()));
        assert_eq!(mock_clock.time, begin + skip);
    }

    #[test]
    fn test_random_increments() {
        set_logger_for_test();
        let mut mock_clock = FakeClock::<()>::new();
        let min = 10.nanos();
        let max = 20.nanos();
        for _ in 0..200 {
            let begin = mock_clock.time;
            let allowed = (begin + min).into_nanos()..(begin + max).into_nanos();
            mock_clock.increment(&Increment::Random(fidl_fuchsia_testing::RandomRange {
                min_rand: min.into_nanos(),
                max_rand: max.into_nanos(),
            }));
            assert!(allowed.contains(&mock_clock.time.into_nanos()));
        }
    }

    fn check_signaled(e: &zx::EventPair) -> bool {
        e.wait_handle(zx::Signals::EVENTPAIR_SIGNALED, zx::Time::from_nanos(0))
            .map(|s| s & zx::Signals::EVENTPAIR_SIGNALED != zx::Signals::NONE)
            .unwrap_or(false)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_signaling() {
        set_logger_for_test();
        let clock_handle = Arc::new(Mutex::new(FakeClock::<()>::new()));
        let mut mock_clock = clock_handle.lock().unwrap();
        let (e1, cli1) = zx::EventPair::create().unwrap();
        let time = mock_clock.time;
        mock_clock.install_event(Arc::clone(&clock_handle), time + 10.millis(), e1);
        let (e2, cli2) = zx::EventPair::create().unwrap();
        mock_clock.install_event(Arc::clone(&clock_handle), time + 20.millis(), e2);
        let (e3, cli3) = zx::EventPair::create().unwrap();
        mock_clock.install_event(Arc::clone(&clock_handle), time, e3);
        // only e3 should've signalled immediately:
        assert!(!check_signaled(&cli1));
        assert!(!check_signaled(&cli2));
        assert!(check_signaled(&cli3));
        // increment clock by 10 millis:
        mock_clock.increment(&Increment::Determined(10.millis().into_nanos()));
        assert!(check_signaled(&cli1));
        assert!(!check_signaled(&cli2));
        // increment clock by another 10 millis and check that e2 is signaled
        mock_clock.increment(&Increment::Determined(10.millis().into_nanos()));
        assert!(check_signaled(&cli3));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_free_running() {
        set_logger_for_test();
        let clock_handle = Arc::new(Mutex::new(FakeClock::<()>::new()));
        let event = {
            let mut mock_clock = clock_handle.lock().unwrap();
            let (event, client) = zx::EventPair::create().unwrap();
            let sched = mock_clock.time + 10.millis();
            mock_clock.install_event(Arc::clone(&clock_handle), sched, event);
            client
        };

        start_free_running(
            &clock_handle,
            DEFAULT_INCREMENTS_MS.millis(),
            Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
        );
        let _ = fasync::OnSignals::new(&event, zx::Signals::EVENT_SIGNALED).await.unwrap();
        stop_free_running(&clock_handle).await;

        // after free running has ended, timer must not be updating anymore:
        let bef = clock_handle.lock().unwrap().time;
        fasync::Timer::new(zx::Time::after(30.millis()).into()).await;
        assert_eq!(clock_handle.lock().unwrap().time, bef);
    }

    struct RemovalObserver {
        sender: mpsc::UnboundedSender<zx::Koid>,
        receiver: Option<mpsc::UnboundedReceiver<zx::Koid>>,
    }

    impl FakeClockObserver for RemovalObserver {
        fn new() -> Self {
            let (sender, r) = mpsc::unbounded();
            Self { sender, receiver: Some(r) }
        }

        fn event_removed(&mut self, koid: Koid) {
            self.sender.unbounded_send(koid).unwrap();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_observes_handle_closed() {
        set_logger_for_test();
        let clock_handle = Arc::new(Mutex::new(FakeClock::<RemovalObserver>::new()));
        let event = {
            let mut mock_clock = clock_handle.lock().unwrap();
            let (event, client) = zx::EventPair::create().unwrap();
            let sched = mock_clock.time + 10.millis();
            mock_clock.install_event(Arc::clone(&clock_handle), sched, event);
            client
        };
        let mut recv = clock_handle.lock().unwrap().observer.receiver.take().unwrap();
        // store the koid
        let koid = event.get_koid().unwrap();
        // dispose of the client side
        std::mem::drop(event);
        assert_eq!(recv.next().await.unwrap(), koid);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reschedule() {
        let clock_handle = Arc::new(Mutex::new(FakeClock::<RemovalObserver>::new()));
        let mut mock_clock = clock_handle.lock().unwrap();
        let (event, client) = zx::EventPair::create().unwrap();
        let sched = mock_clock.time + 10.millis();
        mock_clock.install_event(Arc::clone(&clock_handle), sched, event);
        assert!(!check_signaled(&client));
        // now reschedule the same event:
        let sched = mock_clock.time + 20.millis();
        mock_clock.reschedule_event(sched, client.get_koid().unwrap());
        println!("{:?}", mock_clock.pending_events);
        assert!(!check_signaled(&client));
        // advance time and ensure that we don't fire the event
        mock_clock.increment(&Increment::Determined(10.millis().into_nanos()));
        assert!(!check_signaled(&client));
        mock_clock.increment(&Increment::Determined(10.millis().into_nanos()));
        assert!(check_signaled(&client));
        // clear the signal, reschedule once more and see that it gets hit again.
        client.signal_handle(zx::Signals::EVENTPAIR_SIGNALED, zx::Signals::NONE).unwrap();
        assert!(!check_signaled(&client));
        let sched = mock_clock.time + 10.millis();
        mock_clock.reschedule_event(sched, client.get_koid().unwrap());
        // not yet signaled...
        assert!(!check_signaled(&client));
        // increment once again and it should be signaled then:
        mock_clock.increment(&Increment::Determined(10.millis().into_nanos()));
        assert!(check_signaled(&client));
    }
}
