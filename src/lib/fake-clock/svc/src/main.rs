// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_testing::{
    DeadlineEventType, FakeClockControlRequest, FakeClockControlRequestStream, FakeClockRequest,
    FakeClockRequestStream, Increment,
};
use fidl_fuchsia_testing_deadline::DeadlineId;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::{self as zx, AsHandleRef, DurationNum, Peered};
use futures::{
    stream::{StreamExt, TryStreamExt},
    FutureExt,
};
use log::{debug, error, warn};

use std::collections::{hash_map, BinaryHeap, HashMap};
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

// Ord and Eq implementations provided for use with BinaryHeap.
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
        match self.event.signal_peer(zx::Signals::NONE, zx::Signals::EVENT_SIGNALED) {
            Ok(()) => (),
            Err(zx::Status::PEER_CLOSED) => debug!("Got PEER_CLOSED while signaling an event"),
            Err(e) => error!("Got an unexpected error while signaling an event: {:?}", e),
        }
    }

    fn clear(&mut self) {
        self.pending = false;
        match self.event.signal_peer(zx::Signals::EVENT_SIGNALED, zx::Signals::NONE) {
            Ok(()) => (),
            Err(zx::Status::PEER_CLOSED) => debug!("Got PEER_CLOSED while clearing an event"),
            Err(e) => error!("Got an unexpected error while clearing an event: {:?}", e),
        }
    }
}

#[derive(Eq, PartialEq, Hash, Debug)]
struct StopPoint {
    deadline_id: DeadlineId,
    event_type: DeadlineEventType,
}

struct PendingDeadlineExpireEvent {
    deadline_id: DeadlineId,
    deadline: zx::Time,
}

// Ord and Eq implementations provided for use with BinaryHeap.
impl Eq for PendingDeadlineExpireEvent {}
impl PartialEq for PendingDeadlineExpireEvent {
    fn eq(&self, other: &Self) -> bool {
        self.deadline == other.deadline
    }
}

impl PartialOrd for PendingDeadlineExpireEvent {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for PendingDeadlineExpireEvent {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        other.deadline.cmp(&self.deadline)
    }
}

/// The fake clock implementation.
/// Type parameter `T` is used to observe events during testing.
/// The empty tuple `()` implements `FakeClockObserver` and is meant to be used
/// for production instances.
struct FakeClock<T> {
    time: zx::Time,
    free_running: Option<fasync::Task<()>>,
    pending_events: BinaryHeap<PendingEvent>,
    registered_events: HashMap<zx::Koid, RegisteredEvent>,
    pending_named_deadlines: BinaryHeap<PendingDeadlineExpireEvent>,
    registered_stop_points: HashMap<StopPoint, zx::EventPair>,
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
            pending_named_deadlines: BinaryHeap::new(),
            registered_stop_points: HashMap::new(),
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

    /// Check if a matching stop point is registered and attempts to signal the matching eventpair
    /// if one is registered. Returns true iff a match exists and signaling the event pair succeeds.
    fn check_stop_point(&mut self, stop_point: &StopPoint) -> bool {
        if let Some(stop_point_eventpair) = self.registered_stop_points.remove(&stop_point) {
            match stop_point_eventpair.signal_peer(zx::Signals::NONE, zx::Signals::EVENT_SIGNALED) {
                Ok(()) => true,
                Err(zx::Status::PEER_CLOSED) => {
                    debug!("Got PEER_COSED while signaling a named event");
                    false
                }
                Err(e) => {
                    error!("Failed to signal named event: {:?}", e);
                    false
                }
            }
        } else {
            false
        }
    }

    /// Check if any expired stop points are registered and signal any that exist. Returns true iff
    /// at least one is expired and has been successfully signaled.
    fn check_stop_points(&mut self) -> bool {
        let mut stop_time = false;
        while let Some(e) = self.pending_named_deadlines.peek() {
            if e.deadline <= self.time {
                let stop_point = StopPoint {
                    deadline_id: self.pending_named_deadlines.pop().unwrap().deadline_id,
                    event_type: DeadlineEventType::Expired,
                };
                if self.check_stop_point(&stop_point) {
                    stop_time = true;
                }
            } else {
                break;
            }
        }
        stop_time
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
        fasync::Task::local(closed_fut).detach();
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

    /// Set a stop point at which to stop time and signal the provided `eventpair`.
    /// Returns `ZX_ALREADY_BOUND` if an identical stop point is already registered.
    fn set_stop_point(
        &mut self,
        stop_point: StopPoint,
        eventpair: zx::EventPair,
    ) -> Result<(), zx::Status> {
        match self.registered_stop_points.entry(stop_point) {
            hash_map::Entry::Occupied(mut occupied) => {
                match occupied.get().wait_handle(zx::Signals::EVENTPAIR_CLOSED, zx::Time::ZERO) {
                    Ok(_) => {
                        // Okay to replace an eventpair if the other end is already closed.
                        let _previous = occupied.insert(eventpair);
                        Ok(())
                    }
                    Err(zx::Status::TIMED_OUT) => {
                        warn!("Received duplicate interest in stop point {:?}.", occupied.key());
                        Err(zx::Status::ALREADY_BOUND)
                    }
                    Err(e) => {
                        error!("Got an error while checking signals on an eventpair: {:?}", e);
                        Err(zx::Status::ALREADY_BOUND)
                    }
                }
            }
            hash_map::Entry::Vacant(vacant) => {
                let _value: &mut zx::EventPair = vacant.insert(eventpair);
                Ok(())
            }
        }
    }

    fn add_named_deadline(&mut self, pending_deadline: PendingDeadlineExpireEvent) {
        let () = self.pending_named_deadlines.push(pending_deadline);
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
        let () = self.check_events();
        if self.check_stop_points() {
            let () = self.stop_free_running();
        }
    }

    fn stop_free_running(&mut self) {
        // Dropping the task stops it being polled.
        drop(self.free_running.take());
    }
}

type FakeClockHandle<T> = Arc<Mutex<FakeClock<T>>>;

fn start_free_running<T: FakeClockObserver>(
    mock_clock: &FakeClockHandle<T>,
    real_increment: zx::Duration,
    increment: Increment,
) {
    let mock_clock_clone = Arc::clone(&mock_clock);

    mock_clock.lock().unwrap().free_running = Some(fasync::Task::local(async move {
        let mut itv = fasync::Interval::new(real_increment);
        debug!("free running mock clock {:?} {:?}", real_increment, increment);
        loop {
            itv.next().await;
            mock_clock_clone.lock().unwrap().increment(&increment);
        }
    }));
}

fn stop_free_running<T: FakeClockObserver>(mock_clock: &FakeClockHandle<T>) {
    mock_clock.lock().unwrap().stop_free_running();
}

fn check_valid_increment(increment: &Increment) -> bool {
    match increment {
        Increment::Determined(_) => true,
        Increment::Random(rr) => rr.min_rand >= 0 && rr.max_rand >= 0 && rr.max_rand > rr.min_rand,
    }
}

async fn handle_control_events<T: FakeClockObserver>(
    mock_clock: FakeClockHandle<T>,
    rs: FakeClockControlRequestStream,
) -> Result<(), fidl::Error> {
    rs.try_for_each(|req| async {
        match req {
            FakeClockControlRequest::Advance { increment, responder } => {
                if check_valid_increment(&increment) {
                    let mut mc = mock_clock.lock().unwrap();
                    if mc.is_free_running() {
                        responder.send(&mut Err(zx::Status::ACCESS_DENIED.into_raw()))
                    } else {
                        mc.increment(&increment);
                        responder.send(&mut Ok(()))
                    }
                } else {
                    responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()))
                }
            }
            FakeClockControlRequest::Pause { responder } => {
                stop_free_running(&mock_clock);
                responder.send()
            }
            FakeClockControlRequest::ResumeWithIncrements { real, increment, responder } => {
                if real <= 0 || !check_valid_increment(&increment) {
                    responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()))
                } else {
                    // stop free running if we are
                    stop_free_running(&mock_clock);
                    start_free_running(&mock_clock, real.nanos(), increment);
                    responder.send(&mut Ok(()))
                }
            }
            FakeClockControlRequest::AddStopPoint {
                deadline_id,
                event_type,
                on_stop,
                responder,
            } => {
                debug!("stop point of type {:?} registered", event_type);
                let mut mc = mock_clock.lock().unwrap();
                if mc.is_free_running() {
                    responder.send(&mut Err(zx::Status::ACCESS_DENIED.into_raw()))
                } else {
                    responder.send(
                        &mut mc
                            .set_stop_point(StopPoint { deadline_id, event_type }, on_stop)
                            .map_err(zx::Status::into_raw),
                    )
                }
            }
        }
    })
    .await
}

async fn handle_events<T: FakeClockObserver>(
    mock_clock: FakeClockHandle<T>,
    rs: FakeClockRequestStream,
) -> Result<(), fidl::Error> {
    rs.try_for_each(|req| async {
        match req {
            FakeClockRequest::RegisterEvent { time, event, control_handle: _ } => {
                mock_clock.lock().unwrap().install_event(
                    Arc::clone(&mock_clock),
                    zx::Time::from_nanos(time),
                    event.into(),
                );
                Ok(())
            }
            FakeClockRequest::Get { responder } => {
                responder.send(mock_clock.lock().unwrap().time.into_nanos())
            }
            FakeClockRequest::RescheduleEvent { event, time, responder } => {
                if let Ok(k) = event.get_koid() {
                    mock_clock.lock().unwrap().reschedule_event(zx::Time::from_nanos(time), k)
                }
                responder.send()
            }
            FakeClockRequest::CancelEvent { event, responder } => {
                if let Ok(k) = event.get_koid() {
                    mock_clock.lock().unwrap().cancel_event(k);
                }
                responder.send()
            }
            FakeClockRequest::CreateNamedDeadline { id, duration, responder } => {
                debug!("Creating named deadline with id {:?}", id);
                let stop_point =
                    StopPoint { deadline_id: id.clone(), event_type: DeadlineEventType::Set };
                if mock_clock.lock().unwrap().check_stop_point(&stop_point) {
                    stop_free_running(&mock_clock);
                }

                let deadline = mock_clock.lock().unwrap().time + zx::Duration::from_nanos(duration);
                let expiration_point =
                    PendingDeadlineExpireEvent { deadline_id: id, deadline: deadline };
                mock_clock.lock().unwrap().add_named_deadline(expiration_point);

                responder.send(deadline.into_nanos())
            }
        }
    })
    .await
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
            fasync::Task::local(async move {
                match handle_control_events(cl, rs).await {
                    Ok(()) => (),
                    Err(e) if e.is_closed() => {
                        debug!("Got channel closed while serving fake clock control: {:?}", e)
                    }
                    Err(e) => {
                        error!("Got unexpected error while serving fake clock control: {:?}", e)
                    }
                }
            })
            .detach()
        })
        .add_fidl_service(move |rs: FakeClockRequestStream| {
            let cl = Arc::clone(&m1);
            fasync::Task::local(async move {
                match handle_events(cl, rs).await {
                    Ok(()) => (),
                    Err(e) if e.is_closed() => {
                        debug!("Got channel closed while serving fake clock : {:?}", e)
                    }
                    Err(e) => error!("Got unexpected error while serving fake clock: {:?}", e),
                }
            })
            .detach()
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
    use named_timer::DeadlineId;
    use std::sync::Once;

    const DEADLINE_ID: DeadlineId<'static> = DeadlineId::new("component_1", "code_1");
    const DEADLINE_ID_2: DeadlineId<'static> = DeadlineId::new("component_1", "code_2");

    /// log::Log implementation that uses stdout.
    ///
    /// Useful when debugging tests.
    struct Logger;

    impl log::Log for Logger {
        fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
            true
        }

        fn log(&self, record: &log::Record<'_>) {
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
        let time = zx::Time::get_monotonic();
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
        stop_free_running(&clock_handle);

        // after free running has ended, timer must not be updating anymore:
        let bef = clock_handle.lock().unwrap().time;
        fasync::Timer::new(zx::Time::after(30.millis())).await;
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

    #[fasync::run_singlethreaded(test)]
    async fn test_stop_points() {
        let clock_handle = Arc::new(Mutex::new(FakeClock::<RemovalObserver>::new()));
        let (client_event, server_event) = zx::EventPair::create().unwrap();
        let () = clock_handle
            .lock()
            .unwrap()
            .set_stop_point(
                StopPoint { deadline_id: DEADLINE_ID.into(), event_type: DeadlineEventType::Set },
                server_event,
            )
            .expect("set stop point failed");
        let () = start_free_running(
            &clock_handle,
            DEFAULT_INCREMENTS_MS.millis(),
            Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
        );
        // Checking for the stop point should signal the event pair.
        assert!(clock_handle.lock().unwrap().check_stop_point(&StopPoint {
            deadline_id: DEADLINE_ID.into(),
            event_type: DeadlineEventType::Set
        }));
        assert!(check_signaled(&client_event));
        let () = stop_free_running(&clock_handle);

        // A deadline set to expire in the future stops time when the deadline is reached.
        let future_deadline_timeout = clock_handle.lock().unwrap().time + 10.millis();
        let () = clock_handle.lock().unwrap().add_named_deadline(PendingDeadlineExpireEvent {
            deadline_id: DEADLINE_ID.into(),
            deadline: future_deadline_timeout,
        });
        let (client_event, server_event) = zx::EventPair::create().unwrap();
        let () = clock_handle
            .lock()
            .unwrap()
            .set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID.into(),
                    event_type: DeadlineEventType::Expired,
                },
                server_event,
            )
            .expect("set stop point failed");
        let () = start_free_running(
            &clock_handle,
            DEFAULT_INCREMENTS_MS.millis(),
            Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
        );
        assert_eq!(
            fasync::OnSignals::new(&client_event, zx::Signals::EVENTPAIR_SIGNALED).await.unwrap()
                & !zx::Signals::EVENTPAIR_CLOSED,
            zx::Signals::EVENTPAIR_SIGNALED
        );
        assert!(!clock_handle.lock().unwrap().is_free_running());
        assert_eq!(clock_handle.lock().unwrap().time, future_deadline_timeout);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ignored_stop_points() {
        let clock_handle = Arc::new(Mutex::new(FakeClock::<RemovalObserver>::new()));
        let () = start_free_running(
            &clock_handle,
            DEFAULT_INCREMENTS_MS.millis(),
            Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
        );
        // Checking for an unregistered stop point should not stop time.
        assert!(!clock_handle.lock().unwrap().check_stop_point(&StopPoint {
            deadline_id: DEADLINE_ID.into(),
            event_type: DeadlineEventType::Set
        }));
        assert!(clock_handle.lock().unwrap().is_free_running());

        // Time is not stopped if the other end of a registered event pair is dropped.
        let (client_event, server_event) = zx::EventPair::create().unwrap();
        let () = clock_handle
            .lock()
            .unwrap()
            .set_stop_point(
                StopPoint { deadline_id: DEADLINE_ID.into(), event_type: DeadlineEventType::Set },
                server_event,
            )
            .expect("set stop point failed");
        let () = start_free_running(
            &clock_handle,
            DEFAULT_INCREMENTS_MS.millis(),
            Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
        );
        drop(client_event);
        assert!(!clock_handle.lock().unwrap().check_stop_point(&StopPoint {
            deadline_id: DEADLINE_ID.into(),
            event_type: DeadlineEventType::Set
        }));
        assert!(clock_handle.lock().unwrap().is_free_running());
        let () = stop_free_running(&clock_handle);

        // If we set two EXPIRED points and drop the handle of the earlier one, time should stop
        // on the later stop point.
        let future_deadline_timeout_1 = clock_handle.lock().unwrap().time + 10.millis();
        let future_deadline_timeout_2 = clock_handle.lock().unwrap().time + 20.millis();
        let () = clock_handle.lock().unwrap().add_named_deadline(PendingDeadlineExpireEvent {
            deadline_id: DEADLINE_ID.into(),
            deadline: future_deadline_timeout_1,
        });
        let () = clock_handle.lock().unwrap().add_named_deadline(PendingDeadlineExpireEvent {
            deadline_id: DEADLINE_ID_2.into(),
            deadline: future_deadline_timeout_2,
        });
        let (client_event_1, server_event_1) = zx::EventPair::create().unwrap();
        let () = clock_handle
            .lock()
            .unwrap()
            .set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID.into(),
                    event_type: DeadlineEventType::Expired,
                },
                server_event_1,
            )
            .expect("set stop point failed");
        let (client_event_2, server_event_2) = zx::EventPair::create().unwrap();
        let () = clock_handle
            .lock()
            .unwrap()
            .set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID_2.into(),
                    event_type: DeadlineEventType::Expired,
                },
                server_event_2,
            )
            .expect("set stop point failed");
        drop(client_event_1);
        let () = start_free_running(
            &clock_handle,
            DEFAULT_INCREMENTS_MS.millis(),
            Increment::Determined(DEFAULT_INCREMENTS_MS.millis().into_nanos()),
        );
        assert_eq!(
            fasync::OnSignals::new(&client_event_2, zx::Signals::EVENTPAIR_SIGNALED).await.unwrap()
                & !zx::Signals::EVENTPAIR_CLOSED,
            zx::Signals::EVENTPAIR_SIGNALED
        );
        assert!(!clock_handle.lock().unwrap().is_free_running());
        assert_eq!(clock_handle.lock().unwrap().time, future_deadline_timeout_2);
    }

    #[fuchsia::test]
    fn duplicate_stop_points_rejected() {
        let mut clock = FakeClock::<()>::new();
        let (client_event_1, server_event_1) = zx::EventPair::create().unwrap();
        assert!(clock
            .set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID.into(),
                    event_type: DeadlineEventType::Expired
                },
                server_event_1
            )
            .is_ok());

        let (client_event_2, server_event_2) = zx::EventPair::create().unwrap();
        assert_eq!(
            clock.set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID.into(),
                    event_type: DeadlineEventType::Expired
                },
                server_event_2
            ),
            Err(zx::Status::ALREADY_BOUND)
        );

        // original can still be signaled.
        assert!(clock.check_stop_point(&StopPoint {
            deadline_id: DEADLINE_ID.into(),
            event_type: DeadlineEventType::Expired
        }));
        assert!(check_signaled(&client_event_1));
        assert!(!check_signaled(&client_event_2));
    }

    #[fuchsia::test]
    fn duplicate_stop_point_accepted_if_initial_closed() {
        let mut clock = FakeClock::<()>::new();
        let (client_event_1, server_event_1) = zx::EventPair::create().unwrap();
        assert!(clock
            .set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID.into(),
                    event_type: DeadlineEventType::Expired
                },
                server_event_1
            )
            .is_ok());

        drop(client_event_1);
        let (client_event_2, server_event_2) = zx::EventPair::create().unwrap();
        assert!(clock
            .set_stop_point(
                StopPoint {
                    deadline_id: DEADLINE_ID.into(),
                    event_type: DeadlineEventType::Expired
                },
                server_event_2
            )
            .is_ok());

        // The later eventpair is signaled when checking a stop point.
        assert!(clock.check_stop_point(&StopPoint {
            deadline_id: DEADLINE_ID.into(),
            event_type: DeadlineEventType::Expired
        }));
        assert!(check_signaled(&client_event_2));
    }
}
